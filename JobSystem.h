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
#include "JobBarrier.h"
#include "JobDebugger.h"

namespace ECS::JobSystem{
   
    //using JobHandle = std::pair<TaskPtr,std::unique_ptr<IFuture>>;

template<typename Recorder = NullRecorder>
class JobManager
{
    using JobQueue = Debug::DebugJobQueue<JobDeque>;

public:

    explicit JobManager(size_t threadCount,
        Recorder* rec = nullptr,
        size_t capacity = 1024)
        : recorder(rec),
        stopFlag(false),
        nextQueue(0),
        jobBarrier(threadCount + 1)
    {
        ASSERT(threadCount > 0, "JobSystem is ThreadCount <= 0");

        jobQueues.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            jobQueues.emplace_back(
                std::make_unique<JobQueue>(capacity,i)
            );
        }

        // workers 初期化
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this, i]() noexcept {

                //jobBarrier.wait();

                this->workerThreadFunction(i);
                });
        }

        //jobBarrier.wait();
    }

    ~JobManager(){
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

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                 if constexpr (std::is_void_v<R>) {
                    fn();           
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        if (t->inDegree.load() == 0) {
            //std::lock_guard lk(wakeMutex);
            pushBottom(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //通常Job追加(依存Task)
    template<typename F>
    auto schedule_job(F&& func,const std::vector<TaskPtr>& deps) {

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                if constexpr (std::is_void_v<R>) {
                    fn();            
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        for (auto &d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d&&d->job.valid()) {
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

    template<size_t BufSize>
    void schedule_parallelJob(std::vector<ParallelJob<BufSize>>&& jobs) {

        auto jobsPtr
            = std::make_shared<std::vector<ParallelJob<BufSize>>>(
                std::move(jobs)
                );

        for (uint32_t i = 0; i < jobsPtr->size(); ++i) {


            TaskPtr t{ new Task(
                Job([jobsPtr,i]() mutable {
                        (*jobsPtr)[i].invoke();
                }
                ),
                0
            ) };

            pushBottom(t);
        }
    }

    template<size_t BufSize>
    void schedule_parallelJob(char name,std::vector<ParallelJob<BufSize>>&&jobs) {

       auto jobsPtr
           = std::make_shared<std::vector<ParallelJob<BufSize>>>(
               std::move(jobs)
               );

       auto rec = recorder;

       for (uint32_t i = 0; i < jobsPtr->size(); ++i) {

            //auto job = jobsPtr[i];
            TaskPtr t{ new Task(
                Job([jobsPtr,i,name,rec]() mutable {
                        int h = rec ? rec->recordStart(name) : 0;
                        (*jobsPtr)[i].invoke();
                        if (rec) rec->recordEnd(h);
                }
                ),
                0
            )};
            
            pushBottom(t);
       }
        
        /*batches.emplace_back(
                [&](size_t b, size_t l) {

                    int h = recorder ? recorder->recordStart(name) : 0;
                    for (size_t i = b; i < b + l; ++i) fn(i);
                    if (recorder) recorder->recordEnd(h);

                    if(counter->fetch_sub(1) == 1){
                        settable.set_value();
                    }
                },
                begin, len
                    );*/

        /*for()
        TaskPtr t{ new Task(
            Job{ [=]() {
        size_t count = (total + grain - 1) / grain;
        for (size_t chunk = 0; chunk < count; ++chunk) {
            size_t begin = chunk * grain;
            size_t end = min(begin + grain, total);
            for (size_t i = begin; i < end; ++i) {
                f(i);
            }
        }
        } };*/

        

        /*for (auto& d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d && d->job.valid()) {
                addDependent(d.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            pushBottom(t);
        }*/

        //return future;
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
    template<typename F>
    auto schedule(size_t total,size_t grain,char name,
        F&& func) {

        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;
            fn();
            if (recorder) recorder->recordEnd(h);
        };

        return schedule_parallelJob(total,grain,name,std::move(wrapped));
    }

    //debug付き並列Job追加
   /* std::vector<JobHandle> schedule(uint32_t jobCount,char name,
        const std::function<void(uint32_t)>& job, const std::vector<JobHandle>& deps = {}) {
        std::vector<JobHandle> handles(jobCount);
        for (uint32_t jobIndex = 0; jobIndex < jobCount; jobIndex++) {
            auto wrapper = [this,name,job, jobIndex]() {
                auto h = recorder ? recorder->recordStart(name) : 0;
                job(jobIndex);

                if (recorder) recorder->recordEnd(h);
            };

            handles[jobIndex] = schedule(wrapper,deps);
        }

        return handles;
    }*/

    void waitForAll() {
        std::unique_lock<std::mutex> lk(finishMutex);
        finishCv.wait(lk, [&] {
            return abortFlag.load(std::memory_order_acquire)
                || outstanding.load(std::memory_order_acquire) == 0;
            });
    }

    void run_one_job(){
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();

        run_pending_job(idx);
    };

    void workerThreadFunction(size_t queueIndex) {

        const size_t index = queueIndex;
        // 終了フラグと outstanding の組み合わせでループ制御
        while (true) {
            //停止指示または未完了ジョブなしなら抜ける
            if (abortFlag.load(std::memory_order_acquire)) {
                break;
            }

            if (stopFlag.load(std::memory_order_acquire) &&
                outstanding.load(std::memory_order_acquire) == 0)
            {
                break;
            }

            //自キューから pop
            run_pending_job(index);

            if(jobQueues[index]->isAbort()){
                abort();
                return;
            }
        }

        if(abortFlag.load(std::memory_order_acquire) != false){
            run_while_validQueue(index);
        }
    }

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

private:

    void pushBottom(TaskPtr task) {
        
        auto start = std::chrono::steady_clock::now();
        const auto  timeout = std::chrono::milliseconds(2);

        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();
        auto& queue = jobQueues[idx];

        while (true) {
            PushResult res = queue->pushBottom(std::move(task));
            
            switch (res.status) {
                case PushStatus::Success:
                {
                    outstanding.fetch_add(1, std::memory_order_acq_rel);
                    wakeCv.notify_one();
                    return;
                }

                case PushStatus::WouldBlock:
                {
                    if (std::chrono::steady_clock::now() - start >= timeout) {
                        fallbackExecuteOrEnqueue(idx,std::move(res.notPushed));
                        return;
                    }

                    task = std::move(res.notPushed);
                    // 軽めのバックオフ
                    std::this_thread::yield();
                    break;
                }
                case PushStatus::Full:
                {
                    if (std::chrono::steady_clock::now() - start < timeout) {
                        task = std::move(res.notPushed);
                        //少し待って再挑戦
                        std::this_thread::yield();
                        //std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                    else {
                        fallbackExecuteOrEnqueue(idx,std::move(res.notPushed));
                        return;
                    }

                    break;
                }
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

        //自キューからPOP
        {
            auto popRes = jobQueues[queueIndex]->popBottom();

            if (popRes.status == PopStatus::Success){
                runJob(queueIndex,std::move(popRes.value));
                return;
            }else if(popRes.status == PopStatus::WouldBlock){
                return;
            }
        }
        
        //他スレッドからsteal
        //Block時、steal再挑戦にする
        {
            auto stealRes = stealFromOthers(queueIndex);
            
            if(stealRes.status == StealStatus::Success){
                runJob(queueIndex,std::move(stealRes.value));
                return;
            }
        }

        if (!globalQueue.empty()) {
            
            std::optional<TaskPtr> fallback;
            {
                std::lock_guard lk(globalQueueMtx);
                
                if (!globalQueue.empty()) {
                    fallback = globalQueue.back();
                    globalQueue.pop_back();
                }
            }

            if (fallback) {
                runJob(queueIndex, std::move(*fallback));
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
            outstanding.fetch_add(1, std::memory_order_acq_rel);
            globalQueue.push_back(std::optional(std::move(job)));
        }
    }

    void runJob(size_t queueIndex,std::optional<TaskPtr>&& optTask){

        ASSERT(optTask, "runJob optTask is nullopt!!");

        TaskPtr task = std::move(*optTask);

        ASSERT(task&&task->job.valid(), "task is invoked in JobQueue!!");

        task->job.invoke();

        //繋がっているchildの依存カウントを減らしていく
        for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
            if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                pushBottom(child);
            }
        }

        static std::mutex logMutex;

        // runJob 内
        auto prev = outstanding.fetch_sub(1, std::memory_order_acq_rel);
        bool didAllFinish = (prev == 1);

        if (didAllFinish) {
            std::lock_guard<std::mutex> lk(finishMutex);
            finishCv.notify_all();
        }

        {
            std::lock_guard<std::mutex> lk2(logMutex);
            if (didAllFinish) {
                test::saveLog("[All FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
            }
            else {
                test::saveLog("[FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
            }
        }
    }

    void run_while_validQueue(size_t queueIndex) {
         while(true){
             auto popRes = jobQueues[queueIndex]->popBottom();

             if (popRes.status == PopStatus::Success) {
                 runJob(queueIndex, std::move(popRes.value));
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
    void abort(){
        std::lock_guard lk(finishMutex);
        if(!abortFlag.load()){
            abortFlag.store(true, std::memory_order_release);
            finishCv.notify_all();
        }
    };

private:
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<JobQueue>> jobQueues;

    std::vector<std::optional<TaskPtr>> globalQueue;
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

    JobBarrier jobBarrier;

    std::atomic<bool> abortFlag{ false };
};

} //namespace ECS::JobSystem