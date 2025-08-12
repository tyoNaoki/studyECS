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
#include "JobBarrier.h"
#include "JobDebugger.h"

namespace ECS::JobSystem{
   
    //using JobHandle = std::pair<TaskPtr,std::unique_ptr<IFuture>>;

template<typename T>
class WaitQueue {
public:
    void push(T v) {
        std::lock_guard<std::mutex> lk(m);
        q.push(std::move(v));
        cv.notify_one();
    }

    bool try_pop(T& value){
        std::lock_guard<std::mutex> lk(m);
        if (q.empty()) {
            return false;
        }

        value = std::move(q.front());
        q.pop();
        return true;
    }

private:
    // 消費側は単一スレッドを想定
    T pop() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return !q.empty(); });
        T v = std::move(q.front());
        q.pop();
        return v;
    }

private:
    std::queue<T> q;
    std::mutex m;
    std::condition_variable cv;
};

class JobManager
{
    using JobQueue = Debug::DebugJobQueue<JobDeque>;

public:
    static JobManager& Instance() {
        static JobManager manager;
        return manager;
    }

    void Initialize(size_t threadCount,
        std::unique_ptr<TimelineRecorder> rec = nullptr,
        size_t capacity = 1024);

    ~JobManager();

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
            pushWaitQueue(t);
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
            pushWaitQueue(t);
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

    void waitForAll();

    void run_one_job();

    void workerThreadFunction(size_t queueIndex);

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    bool checkRanAllJobInJobQueues();

    IRecorder* getRecorder(){
        if(recorder){
            return recorder.get();
        }

        return nullptr;
    }

    void pushWaitQueue(TaskPtr task);

    const size_t getThreadSize() const{
        return threadSize;
    }

private:

    bool pushBottom(TaskPtr task,size_t idx);
    
    /*void pushBottom(TaskPtr handle){
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();
        if(jobQueues[idx]->pushBottom(handle)){
            std::cout << "[START] queue=" << idx
                << " outstanding=" << outstanding.load()
                << std::endl;

            wakeCv.notify_one();
        }
    }*/

    void pushLocalQueue(size_t queueIndex);

    void run_pending_job(size_t queueIndex);

    //stealTopを使って他ワーカーから奪う
    StealResult stealFromOthers(size_t stealOwner);

    void fallbackWaitQueue(size_t queueIndex,TaskPtr job) {

        waitQueues[queueIndex]->push(std::move(job));
    }

    void runJob(size_t queueIndex,std::optional<TaskPtr>&& optTask);

    void run_while_validQueue(size_t queueIndex);

    bool allQueuesEmpty() const;

    void addDependent(Task* parent, Task* child) {
        // child を親の先頭に差し込む
        child->nextDependent = parent->nextDependent;
        parent->nextDependent = child;
        child->inDegree.fetch_add(1);
    }

    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

private:
    void abort();

private:
    JobManager() = default;

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;

    size_t threadSize;
    bool initFlag;

    std::vector<std::unique_ptr<WaitQueue<TaskPtr>>> waitQueues;

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<JobQueue>> localQueues;

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

    std::unique_ptr<TimelineRecorder> recorder;

    std::atomic<bool> abortFlag{ false };
};

} //namespace ECS::JobSystem