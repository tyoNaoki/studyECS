#pragma once
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <future>
#include "JobRecorder.h"
#include "JobDeque.hpp"
#include <utility>
#include "taskPtr.hpp"

namespace ECS::JobSystem{

    struct Job {

    private:
        // 最大キャプチャ領域
        static constexpr size_t BufferSize = 32;

        // 呼び出し時の関数ポインタ型
        using Invoker = void(*)(void*);

        // 実データ格納＋呼び出し子
        alignas(void*) char  buf[BufferSize];
        Invoker              invoke_fn = nullptr;

    public:
        Job() = default;

        Job(Job&& o) noexcept {
            invoke_fn = o.invoke_fn;
            memcpy(buf, o.buf, BufferSize);
            o.invoke_fn = nullptr;
        }

        // 任意の小さいラムダ／関数オブジェクトをムーブキャプチャ
        template<typename F>
        Job(F&& f) noexcept {
            static_assert(sizeof(F) <= BufferSize,
                "Job function over BufferSize");
            new (buf) F(std::move(f));
            invoke_fn = [](void* p) {
                auto fp = static_cast<F*>(p);
                (*fp)();
            };
        }

        // 一度きりの実行
        void invoke() noexcept {
            if (invoke_fn) {
                invoke_fn(buf);
                invoke_fn = nullptr;
            }
        }

        explicit operator bool() const noexcept {
            return invoke_fn != nullptr;
        }
    };

    struct Task {
        std::atomic<uint32_t> refCount{ 0 };
        Job job;
        std::atomic<int>   inDegree{ 0 };
        Ptr::intrusive_ptr<Task> nextDependent;
        std::mutex taskMutex;

        Task(Job jb, int degree)
            : refCount(0)
            , job(std::move(jb))
            , inDegree(degree)
            , nextDependent(nullptr)
        {}

        void add_ref() { refCount.fetch_add(1, std::memory_order_relaxed); }
        
        void release() {
            if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete this;
            }
        }

       
    };

    inline void intrusive_ptr_add_ref(Task* p) { p->add_ref(); }
    inline void intrusive_ptr_release(Task* p) { p->release(); }

    // 未 specialization：結果を持てる型用
    template<typename T>
    struct FutureInner {
        std::mutex       mtx;
        std::atomic<bool> ready{ false };
        std::optional<T>  result;
    };

    // void 専用 specialization：result を持たない
    template<>
    struct FutureInner<void> {
        std::mutex       mtx;
        std::atomic<bool> ready{ false };
    };

    // 待ち手（読み取り専用ハンドル）
    template<typename T>
    class JobFuture {
        std::shared_ptr<FutureInner<T>> inner;

    public:
        explicit JobFuture(std::shared_ptr<FutureInner<T>> i)
            : inner(std::move(i)){}

        //JobSystem::run_one_pending_job() を呼びつつ待ち
        T get() {
            while (true) {
                // まず mutex を獲得して ready フラグをチェック
                {
                    std::lock_guard lk(inner->mtx);
                    if (inner->ready) {
                        if constexpr (!std::is_void_v<T>) {
                            return std::move(*inner->result);
                        }
                        else {
                            return;
                        }
                    }
                }

                // まだ ready でなければ他ジョブをひとつ消化
                //jobSystem.run_one_pending_job();
            }
        }

        bool isReady() const{
            return inner->ready;
        }
    };

    // 書き込み手（セット専用ハンドル）
    template<typename T>
    class SettableJobFuture {
        std::shared_ptr<FutureInner<T>> inner;
    public:
        explicit SettableJobFuture(std::shared_ptr<FutureInner<T>> i)
            : inner(std::move(i)) {}

        // Futureペアを作って返すユーティリティ
        static auto create() {
            auto ptr = std::make_shared<FutureInner<T>>();
            return std::make_pair(
                SettableJobFuture{ ptr },
                JobFuture<T>{ptr}
            );
        }

        // 実行タスク側が結果をセットする
        void set_value(T v) {
            std::lock_guard lk(inner->mtx);
            inner->result = std::move(v);
            inner->ready = true;
        }
    };

    // void 専用 write-only specialization
    template<>
    class SettableJobFuture<void> {
        std::shared_ptr<FutureInner<void>> inner;
    public:
        explicit SettableJobFuture(std::shared_ptr<FutureInner<void>> i)
            : inner(std::move(i)) {}

        static auto create() {
            auto ptr = std::make_shared<FutureInner<void>>();
            return std::make_pair(
                SettableJobFuture{ ptr },
                JobFuture<void>{ptr}
            );
        }

        // 結果なしの通知だけ
        void set_value() {
            std::lock_guard lk(inner->mtx);
            inner->ready = true;
        }
    };

    using TaskPtr = Ptr::intrusive_ptr<Task>;

    template<typename T>
    using JobHandle = std::pair<TaskPtr,JobFuture<T>>;

template<typename Recorder = NullRecorder>
class JobSystem
{
    using JobQueue = std::unique_ptr<JobDeque<TaskPtr>>;

public:

    explicit JobSystem(size_t threadCount,
        Recorder* rec = nullptr,
        size_t capacity = 1024)
        : recorder(rec),
        stopFlag(false),
        nextQueue(0)
    {
        assert(threadCount > 0, "JobSystem is ThreadCount <= 0");

        jobQueues.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            jobQueues.emplace_back(
                std::make_unique<JobDeque<TaskPtr>>(capacity)
            );
        }

        // workers 初期化
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this, i]() noexcept {
                this->workerThreadFunction(i);
                });
        }
    }

    ~JobSystem(){
        stopFlag.store(true, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(wakeMutex);
            wakeCv.notify_all();           // ワーカー全員を起こす
        }

        for (auto& w : workers) {
            if (w.joinable())
                w.join();
        }

    }

    //通常Job追加
    template<typename F>
    auto schedule_job(F&& func){

        auto [settable, future] = SettableJobFuture<void>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                fn();
                setter.set_value();
            }),
            0
        ) };

        //未処理カウンタ増加
        outstanding.fetch_add(1, std::memory_order_acq_rel);

        if (t->inDegree.load() == 0) {
            std::lock_guard lk(wakeMutex);
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //通常Job追加
    template<typename F,typename T>
    auto schedule_job(F&& func,const std::vector<JobHandle<T>>& deps) {

        auto [settable, future] = SettableJobFuture<void>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                fn();
                setter.set_value();
            }),
            0
        ) };

        //未処理カウンタ増加
        outstanding.fetch_add(1, std::memory_order_acq_rel);

        for (auto &d : deps) {
            std::lock_guard<std::mutex> lk(d.first->taskMutex);
            if (!d.second.isReady()) {
                addDependent(d.first.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            std::lock_guard lk(wakeMutex);
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    template<typename F,typename T>
    auto schedule_with_future(F&& func, const std::vector<JobHandle<T>>& deps){

        using R = std::invoke_result_t<F>;  // func() の戻り値型

        //Future ペアを作成
        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                R r = fn();
                setter.set_value(std::move(r));
            }),
            0
        )};

        //未処理カウンタ増加
        outstanding.fetch_add(1, std::memory_order_acq_rel);

        for (auto& d : deps) {
            std::lock_guard<std::mutex> lk(d.first->taskMutex);
            if(!d.second.isReady()){
                addDependent(d.first.get(), t.get());
            }
        }

        if (t->in_degree.load() == 0) {
            std::lock_guard lk(wakeMutex);
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //debug付きJob追加
    template<typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
    auto schedule(char name,F&& func)-> JobHandle<R> {

        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;
            fn(); 
            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped));
    }

    //debug付きJob追加
    template<typename F,typename R = std::invoke_result_t<std::decay_t<F>>,typename T>
    auto schedule(char name, F&& func, const std::vector<JobHandle<T>>& deps)-> JobHandle<R> {
    
        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;
            fn();
            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped), deps);
    }

    //通常、並列Job追加
    //std::vector<JobHandle> schedule(uint32_t jobCount,
    //    const std::function<void(uint32_t)>& job, const std::vector<JobHandle>& deps = {}) {
    //    std::vector<JobHandle> handles(jobCount);
    //    for (uint32_t jobIndex = 0; jobIndex < jobCount; jobIndex++) {
    //        auto wrapper = [job, jobIndex]() {
    //            job(jobIndex);
    //        };

    //        handles[jobIndex] = schedule(wrapper,deps);
    //    }

    //    return handles;
    //}

    ////debug付き並列Job追加
    //std::vector<JobHandle> schedule(uint32_t jobCount,char name,
    //    const std::function<void(uint32_t)>& job, const std::vector<JobHandle>& deps = {}) {
    //    std::vector<JobHandle> handles(jobCount);
    //    for (uint32_t jobIndex = 0; jobIndex < jobCount; jobIndex++) {
    //        auto wrapper = [this,name,job, jobIndex]() {
    //            auto h = recorder ? recorder->recordStart(name) : 0;
    //            job(jobIndex);

    //            if (recorder) recorder->recordEnd(h);
    //        };

    //        handles[jobIndex] = schedule(wrapper,deps);
    //    }

    //    return handles;
    //}

    void waitForAll() {
        std::unique_lock<std::mutex> lk(finishMutex);
        finishCv.wait(lk, [&] {
            return outstanding.load(std::memory_order_acquire) == 0;
            });
    }

    void workerThreadFunction(size_t queueIndex) {
        const size_t n = jobQueues.size();

        // 終了フラグと outstanding の組み合わせでループ制御
        while (true) {
            //停止指示かつ未完了ジョブなしなら抜ける
            if (stopFlag.load(std::memory_order_acquire) &&
                outstanding.load(std::memory_order_acquire) == 0)
            {
                break;
            }

            std::optional<TaskPtr> opt;

            //自キューから pop
            if (auto p = jobQueues[queueIndex]->popBottom()) {
                opt = std::move(p);
            }
            else {
                //取れなければ他キューから steal
                for (size_t i = 1; i < n; ++i) {
                    size_t idx = (queueIndex + i) % n;
                    if (auto s = jobQueues[idx]->stealTop()) {
                        opt = std::move(s);
                        break;
                    }
                }
            }

            //どちらも取れなければ一旦 yield
            if (!opt) {
                std::this_thread::yield();
                continue;
            }

            //取得できたジョブを実行
            TaskPtr task = std::move(*opt);
            task->job.invoke();

            //繋がっているchildの依存カウントを減らしていく
            for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
                if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1){
                    std::lock_guard lk(wakeMutex);
                    pushBottom(child);
                }
            }

            //完了カウンタを減らし、最後なら通知
            if (outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(finishMutex);
                std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding << std::endl;

                finishCv.notify_all();
            }else{
                std::lock_guard<std::mutex> lk(finishMutex);
                std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding << std::endl;
            }
        }
    }

private:

    
    void pushBottom(TaskPtr handle){
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();
        jobQueues[idx]->pushBottom(handle);
        std::cout   << "[START] queue=" << idx
                    << " outstanding=" << outstanding.load()
                    << std::endl;

        wakeCv.notify_one();
    }

    bool popBottom(size_t queueIndex,std::optional<Job>& out){
        
        auto pop = jobQueues[queueIndex]->popBottom();
        if(pop){
            out = std::move(pop);
            return true;
        }
        
        return false;
    }

    // stealTop を使って他ワーカーから奪う
    bool stealFromOthers(size_t stealOwner, std::optional<TaskPtr>& out) {
        size_t n = jobQueues.size();

        for (size_t i = 1; i < n; ++i) {
            size_t idx = (stealOwner + i) % n;
            if (auto steal = jobQueues[idx]->stealTop()) {
                out = std::move(steal);
                return true;
            }
        }
        return false;
    }

    bool allQueuesEmpty() const {
        for (auto& dq : jobQueues)
            if (!dq->empty()) return false;
        return true;
    }

    void addDependent(Task* parent, Task* child) {
        // child を親の先頭に差し込む
        child->nextDependent = parent->nextDependent;
        parent->nextDependent = child;
        child->inDegree.fetch_add(1);
    }

private:
    std::vector<std::thread> workers;
    std::vector<JobQueue> jobQueues;

    std::atomic<bool> stopFlag;
    std::atomic<size_t> outstanding{ 0 };

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::atomic<size_t>      nextQueue{ 0 };    
    std::condition_variable condition;
    std::mutex        finishMutex;
    std::condition_variable finishCv;

    Recorder* recorder;
    inline static NullRecorder nullrecorder;
};

} //namespace ECS::JobSystem