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
#include <array>
#include "JobRecorder.h"
#include "JobDeque.hpp"
#include <utility>
#include "JobBarrier.h"
#include "JobDebugger.h"
#include "TaskQueue.h"
#include "ChunkAllocator.h"

namespace ECS::JobSystem{

class JobManager
{
    struct Task;

    template<typename T>
    class intrusive_ptr;

    using TaskPtr = intrusive_ptr<Task>;

    static constexpr size_t bufferCap = 20'000;

    //Chunkに詰められるTaskの最大数
    static constexpr size_t ChunkCap = 512;

    //1回の確保でまとめて作るchunkの数
    static constexpr size_t ChunkMemSize = 14;

    using ChunkAllocator = ChunkAllocator<TaskPtr, ChunkCap, ChunkMemSize,16>;

    using Chunk = ChunkAllocator::Chunk;

    using ChunkPtr = ChunkAllocator::ChunkHandle;

    using WaitBuf = TaskQueue<TaskPtr,bufferCap,ChunkAllocator>;

    using JobQueue = Debug::DebugJobQueue<JobDeque<ChunkPtr>>;

    using StealResult = JobQueue::Base::StealResult;

    using PopResult = JobQueue::Base::PopResult;

    using PushResult = JobQueue::Base::PushResult;

    //仮として60FPS
    static constexpr float targetFPS = 60.0f;

    //1フレームあたりの時間
    static constexpr float frameTimeMs = 1000.0f / targetFPS;

    static constexpr double safetyMarginMs = 1.5;

    static constexpr double bgRatioMin = 0.10;
    static constexpr double bgRatioMax = 0.50;

    JobManager() = default;

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

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
            pushRealTimeJobWaitQueue(t);
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
            pushRealTimeJobWaitQueue(t);
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

    void waitForAllRealTimeJob();

    void waitForLocalBackGroundJob();

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

    //Taskが持つcategoryに応じて対応のwaitQueueにpushする
    void pushJobWaitQueue(TaskPtr task);

    //TaskPtr pushJobWaitQueue(Job&& job,int degree,JobCategory cat);
    
    //まとめてBackGroundJobをpushする用
    void pushBackGroudGlobalQueue(std::vector<TaskPtr>&& tasks);

    //フレーム始めに計算した処理数のBGを各ワーカーごとのBGQueueに割り振る。
    //計算は以下パラメータを使用する。
    //パラメータ(目標FPSms時間、ワンフレームでどのくらいBGを処理するかの比率、1ジョブの平均実行ms時間)
    void popGlobalBackGroundQueue();

    const size_t getThreadSize() const{ return threadSize;}

    void setStartFrameTime();

    std::chrono::steady_clock::time_point getStartFrameTime() const;

private:
    void pushRealTimeJobWaitQueue(TaskPtr task);

    //BGJobをglobalBGQueueにセット
    void pushBackGroudGlobalQueue(TaskPtr task);

    bool pushBottom(ChunkPtr&& chunkPtr, std::unique_ptr<JobQueue>& localQueue, std::unique_ptr<WaitBuf>& waitQueue);

    PopStatus popBottom
    (size_t queueIndex, std::unique_ptr<JobQueue>& queue);

    StealStatus stealQueues(size_t queueIndex, std::vector<std::unique_ptr<JobQueue>>& stealQueues);

    void pushLocalQueue(std::unique_ptr<JobQueue>&localQueue, std::unique_ptr<WaitBuf>&waitQueue);

    //GlobalBGQueueが空ならNull
    std::optional<TaskPtr>try_popGlobalBackGroundQueue();

    void run_realTimeQueue(size_t queueIndex);

    void run_backGroundQueue(size_t queueIndex);

    bool pop_and_steal_Queue(size_t queueIndex,std::vector<std::unique_ptr<JobQueue>>&stealQueues,std::function<void()>sub_counterFunc);

    void fallbackWaitQueue(std::unique_ptr<WaitBuf>& waitQueue,ChunkPtr chunkPtr) {
       waitQueue->push(std::move(chunkPtr));
    }

    void runChunk(size_t queueIndex,ChunkPtr&& chunkPtr);

    void runJob(size_t queueIndex,TaskPtr&& task);

    void run_while_validQueue(size_t queueIndex);

    bool allQueuesEmpty() const;

    void addDependent(Task* parent, Task* child) {
        // child を親の先頭に差し込む
        child->nextDependent = parent->nextDependent;
        parent->nextDependent = child;
        child->inDegree.fetch_add(1);
    }

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

    void sub_realTimeJob_counter();

    void sub_backGroundJob_counter();

private:
    double avg_JobTimeMs = 1.0f;
    double avg_ExecuteJobTime = 0.1;

    std::unique_ptr<ChunkAllocator>allocator;

    // 時刻
    std::chrono::steady_clock::time_point frameStart;

    size_t threadSize;
    bool initFlag;

    std::vector<TaskPtr>globalBackGroudQueue;
    std::mutex backGroundMutex;

    //バックグラウンドで少しづつ処理される
    //全ての待機キューを処理時に個数を決めて取り出す。
    //処理フレームを問わない。
    //std::vector<std::unique_ptr<WaitQueue<TaskPtr>>>backGroundWaitQueues;
    std::vector<std::unique_ptr<WaitBuf>>backGroundWaitQueues;
    std::vector<std::unique_ptr<JobQueue>> backGroundLocalQueue;

    //リアルタイムキュー
    //優先的に処理される
    //1フレーム以内に処理を保証
    std::vector<std::unique_ptr<WaitBuf>> realTimeWaitQueues;
    std::vector<std::unique_ptr<JobQueue>> realTimeLocalQueue;

    std::vector<std::thread> workers;

    std::atomic<bool> stopFlag;
    
    //現ジョブの総数
    std::atomic<size_t> outstanding{ 0 };

    std::atomic<size_t> realTimeJobCounter;
    std::atomic<size_t> backGroundCounter;

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::mutex            realTimeJob_Mutex;
    std::condition_variable realTimeJob_WaitCv;

    std::mutex            backGroundJob_Mutex;
    std::condition_variable backGroundJob_WaitCv;

    std::atomic<size_t>      nextQueue{ 0 };
    std::condition_variable condition;
    std::mutex        finishMutex;
    std::condition_variable finishCv;

    std::unique_ptr<TimelineRecorder> recorder;

    std::atomic<bool> abortFlag{ false };
};

} //namespace ECS::JobSystem