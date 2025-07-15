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

    struct IFuture {
        virtual bool isReady() const = 0;
        virtual void wait() = 0;
    };

    // 待ち手（読み取り専用ハンドル）
    template<typename T>
    struct JobFuture : public IFuture {
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

        void wait() override{
            return;
        }

        bool isReady() const override{
            return inner->ready;
        }

    private:
        std::shared_ptr<FutureInner<T>> inner;
    };

    //voidバージョン
    template<>
    struct JobFuture<void> : public IFuture {
        explicit JobFuture(std::shared_ptr<FutureInner<void>> i)
            : inner(std::move(i)) {}

        void wait() override {
            return;
        }

        bool isReady() const override {
            return inner->ready;
        }

    private:
        std::shared_ptr<FutureInner<void>> inner;
    };

    // 書き込み手（セット専用ハンドル）
    template<typename T>
    struct SettableJobFuture{
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

        private:
            std::shared_ptr<FutureInner<T>> inner;
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

   

    //using JobHandle = std::pair<TaskPtr,std::unique_ptr<IFuture>>;

template<typename Recorder = NullRecorder>
class JobSystem
{
    using JobQueue = std::unique_ptr<JobDeque>;

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
                std::make_unique<JobDeque>(capacity)
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

        using R = std::invoke_result_t<F>;  // func() の戻り値型

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                fn();
                setter.set_value();
            }),
            0
        ) };

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
    template<typename F>
    auto schedule_job(F&& func,const std::vector<TaskPtr>& deps) {

        using R = std::invoke_result_t<F>;  // func() の戻り値型

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                fn();
                setter.set_value();
            }),
            0
        ) };

        for (auto &d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d->job) {
                addDependent(d.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    template<typename F>
    auto schedule_with_future(F&& func, const std::vector<TaskPtr>& deps){

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

        for (auto& d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if(d->job){
                addDependent(d.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //debug付きJob追加
    template<typename F>
    auto schedule(char name,F&& func){

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
    template<typename F>
    auto schedule(char name, F&& func, const std::vector<TaskPtr>& deps){
    
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

        const size_t index = queueIndex;
        // 終了フラグと outstanding の組み合わせでループ制御
        while (true) {
            //停止指示かつ未完了ジョブなしなら抜ける
            if (stopFlag.load(std::memory_order_acquire) &&
                outstanding.load(std::memory_order_acquire) == 0)
            {
                break;
            }

            //自キューから pop
            run_pending_job(index);
        }

        run_while_validQueue(index);
    }

private:
    void pushBottom(TaskPtr task) {
        
        auto start = std::chrono::steady_clock::now();
        const auto  timeout = std::chrono::milliseconds(2);

        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();
        auto& queue = jobQueues[idx];

        while (true) {
            PushResult res queue->pushBottom(std::move(task));

            //{
            //    //std::lock_guard lk(wakeMutex);
            //    res = queue->pushBottom(std::move(task));
            //}
            
            switch (res.status) {
                case PushStatus::Success:
                    //未処理カウンタ増加
                    outstanding.fetch_add(1, std::memory_order_acq_rel);
                    /*std::cout << "[START] queue=" << idx
                        << " outstanding=" << outstanding.load(std::memory_order_acquire)
                        << std::endl;*/
                    wakeCv.notify_one();
                    return;

                case PushStatus::WouldBlock:
                    if (std::chrono::steady_clock::now() - start >= timeout) {
                        fallbackExecuteOrEnqueue(idx,std::move(res.notPushed));
                        return;
                    }
                    // 軽めのバックオフ
                    std::this_thread::yield();
                    break;

                case PushStatus::Full:
                    
                    if (std::chrono::steady_clock::now() - start < timeout) {
                        //少し待って再挑戦
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                    else {
                        fallbackExecuteOrEnqueue(idx,std::move(res.notPushed));
                        return;
                    }

                    break;
            }

        }
    }
    
    /*void pushBottom(TaskPtr handle){
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();
        if(jobQueues[idx]->pushBottom(handle)){
            std::cout << "[START] queue=" << idx
                << " outstanding=" << outstanding.load()
                << std::endl;

            wakeCv.notify_one();
        }
    }*/

    void run_pending_job(size_t queueIndex){
        const size_t n = jobQueues.size();
        std::optional<TaskPtr> opt;

        //自キューからPOP
        {
            auto popRes = jobQueues[queueIndex]->popBottom();

            if (popRes.status == PopStatus::Success){
                runJob(queueIndex,popRes.value);
                return;
            }else if(popRes.status == PopStatus::WouldBlock){
                //再挑戦
                std::this_thread::yield();
                return;
            }
        }
        
        {
            auto stealRes = stealFromOthers(queueIndex);
            
            if(stealRes.status == StealStatus::Success){
                runJob(queueIndex,stealRes.value);
                return;
            }
        }

        //どちらも取れなければ一旦 yield
        std::this_thread::yield();
    }

    //stealTopを使って他ワーカーから奪う
    StealResult stealFromOthers(size_t stealOwner) {
        size_t n = jobQueues.size();

        StealResult result;
        for (size_t i = 1; i < n; ++i) {
            size_t idx = (stealOwner + i) % n;
            result  = jobQueues[idx]->stealTop();

            if (result.status == StealStatus::Success) {
                return result;
            }
        }

        return { StealStatus::Empty, std::nullopt };
    }

    void fallbackExecuteOrEnqueue(size_t queueIndex,TaskPtr job) {
        const size_t MaxFallbackTrials = jobQueues.size();

        //他ワーカーのローカルキューを数回トライ
        for (size_t trial = 1; trial < MaxFallbackTrials; ++trial) {
            size_t idx = (queueIndex + trial) % MaxFallbackTrials;
            auto res = jobQueues[idx]->pushBottom(job);
            if (res.status == PushStatus::Success) {
                outstanding.fetch_add(1, std::memory_order_acq_rel);
                wakeCv.notify_one();
                return;
            }
            if (res.status != PushStatus::Success) {
                job = std::move(res.notPushed);
                continue;
            }
        }

        //グローバルキューへフォールバック
        {
            std::lock_guard lk(globalQueueMtx);
            globalQueue.push_back(std::move(job));
        }
    }

    void runJob(size_t queueIndex,std::optional<TaskPtr>& optTask){

        assert(optTask, "runJob optTask is nullopt!!");

        TaskPtr task = std::move(*optTask);

        assert(task->job, "task is invoked in JobQueue!!");

        task->job.invoke();

        //繋がっているchildの依存カウントを減らしていく
        for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
            if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                pushBottom(child);
            }
        }

        //完了カウンタを減らし、最後なら通知
        if (outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(finishMutex);
            std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding.load(std::memory_order_acquire) << std::endl;

            finishCv.notify_all();
        }
        else {
            //std::lock_guard<std::mutex> lk(finishMutex);
            std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding.load(std::memory_order_acquire) << std::endl;
        }
    }

    void run_while_validQueue(size_t queueIndex) {
         while(true){
             auto popRes = jobQueues[queueIndex]->popBottom();

             if (popRes.status == PopStatus::Success) {
                 runJob(queueIndex, popRes.value);
                 continue;
             }
             
             if (popRes.status == PopStatus::Empty) {
                 break;
             }
         }
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

    bool canInvorkJob(const std::vector<TaskPtr>& deps){
        for (auto& d : deps) {
            if (d->job) {
                return false;
            }
        }

        return true;
    }

private:
    std::vector<std::thread> workers;
    std::vector<JobQueue> jobQueues;

    std::vector<TaskPtr> globalQueue;
    std::mutex globalQueueMtx;

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