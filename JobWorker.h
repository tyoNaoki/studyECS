#pragma once
#include "JobDeque.hpp"

namespace ECS::JobSystem{

struct JobExecutor {
    using TaskPtr = intrusive_ptr<Task>;

    template<typename T>
    void operator()(size_t workerId,T&& chunk) {
        runChunk(workerId,std::move(chunk));
    }

private:
    void runJob(size_t workerId,TaskPtr& task) {
        ASSERT(task && task->job.valid(), "task is invoked in JobQueue!!");

        task->job.invoke();

        //繋がっているchildの依存カウントを減らしていく
        for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
            if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                //pushJobWaitQueue(child);
            }
        }
    }

    template<typename T>
    void runChunk(size_t workerId,T&& chunk) {
        ASSERT(chunk, "chunkPtr is nullptr!!");

        auto start = chunk.start;
        auto end = chunk.count;

        for (size_t i = start; i < end; i++) {
            auto task = chunk.owner->tryGetPtr(i);

            ASSERT(task, "task is nullptr");
            runJob(workerId, task);

            //デストラクタ呼び出し
            chunk.owner->destroyAt(i);
        }
    }
};

struct RealTimePolicy{

    template<typename LocalQ, typename StealQs, typename RealTimeTasks, typename BackGroundTasks, typename Chunk>
    bool operator()(LocalQ& localQ, StealQs& stealQs, RealTimeTasks& rt, BackGroundTasks& bg, Chunk& chunkHandle)
    {
        if(localQ.isAbort()){
            return false;
        }

        Chunk chunk;
        auto& manager = JobManager::Instance();

        size_t takeNum = rt.remainingSlot();

        //待機キューから取得
        std::vector<Chunk> chunks = rt.popMany(takeNum);
        auto chunks = rt.popMany(takeNum);
        while (!chunks.empty()) {
            auto chunk = std::move(chunks.back());

            chunks.pop_back();

            localQ.pushWithTimeout(std::move(chunk), rt);
        }

        //realTimeJob処理
        if (manager.pendingJobCount(JobCategory::RealTime) >  0 && localQ->popOrSteal(stealQs,chunkHandle)) return true;

        takeNum = bg.remainingSlot();

        //待機キューから取得
        std::vector<Chunk> chunks = bg.popMany(takeNum);
        auto chunks = bg.popMany(takeNum);
        while (!chunks.empty()) {
            auto chunk = std::move(chunks.back());

            chunks.pop_back();

            localQ.pushWithTimeout(std::move(chunk), bg);
        }
        
        //backGroundJob処理
        if (manager.pendingJobCount(JobCategory::BackGround) <= 0) return false;

        return localQ->popOrSteal(stealQs,chunkHandle);
    }

    static constexpr size_t realTimeCap = 20'000;
    static constexpr size_t backGroundCap = 1'000;

    static constexpr size_t realTimeChunkSize = 512;
    static constexpr size_t backGroundChunkSize = 512;

    static constexpr size_t localQueueCap = 1024;
};

//struct BackGroundPolicy {
//
//    template<typename LocalQ,typename StealQs,typename RealTimeTasks, typename BackGroundTasks, typename Chunk>
//    bool operator()(LocalQ& localQ, StealQs& stealQs, RealTimeTasks& rt, BackGroundTasks& bg, Chunk& chunkHandle)
//    {
//        if (localQ.isAbort()) {
//            return false;
//        }
//
//        Chunk chunk;
//        auto& manager = JobManager::Instance();
//
//        size_t takeNum = rt.remainingSlot();
//
//        //待機キューから取得
//        while (true) {
//            bg.popMany(takeNum);
//            if (! || !localQ.pushWithTimeout(chunk, bgQ)) break;
//        }
//
//        //backGroundJob処理
//        if (manager.pendingJobCount(JobCategory::BackGround) > 0 && localQ.popOrSteal(stealQs, chunkHandle){
//            return true;
//        }
//
//        //backGroundJobのpopもstealも失敗した場合
//        while (true) {
//            if (!rtQ.try_pop(chunk) || !localQ.pushWithTimeout(chunk, rtQ)) break;
//        }
//
//        //realTimeJob処理
//        if (manager.pendingJobCount(JobCategory::RealTime) == 0) return false;
//
//        return localQ.popOrSteal(stealQs,chunkHandle);
//    }
//
//    static constexpr size_t realTimeCap = 20'000;
//    static constexpr size_t backGroundCap = 1'000;
//
//    static constexpr size_t realTimeChunkSize = 512;
//    static constexpr size_t backGroundChunkSize = 512;
//
//    static constexpr size_t localQueueCap = 1024;
//};

using TaskPtr = intrusive_ptr<Task>;

class JobStats;

//タスクキュー
template<typename Policy>
using RealTimeStorageType= TaskStorage<TaskPtr, Policy::realTimeCap, Policy::realTimeChunkSize>;

template<typename Policy>
using BackGroundStorageType = TaskStorage<TaskPtr, Policy::backGroundCap, Policy::backGroundChunkSize>;

class IWorker {
public:

    virtual ~IWorker() = default;

    virtual TaskPtr schedule(JobCategory cat,
        Job&& job,
        int degree) = 0;

    virtual TaskPtr schedule(JobCategory cat,
        Job&& job,
        int degree,
        std::vector<TaskPtr>& deps) = 0;

    virtual TaskPtr schedule(JobCategory cat,
        TaskPtr&&task) = 0;
};

template<
    typename WorkerPolicy = RealTimePolicy,
    typename ExecuteFunc = JobExecutor
    >
class Worker : public IWorker{
    static constexpr std::size_t localQueueCap = WorkerPolicy::localQueueCap;

    using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;

    using LocalQPtr = std::unique_ptr<JobQueue>*;

    using RealTimeTaskStorage = RealTimeStorageType<WorkerPolicy>;

    using BackGroundTaskStorage = BackGroundStorageType<WorkerPolicy>;

public:
    //localQやtaskQの初期化処理など
    Worker(size_t id,LocalQPtr queues,JobStats&stats);

    //残っているtaskの処理
    ~Worker() override;

    TaskPtr schedule(JobCategory cat, Job&& job, int degree) override;

    TaskPtr schedule(JobCategory cat,Job&& job, int degree, std::vector<TaskPtr>&deps) override;

    TaskPtr schedule(JobCategory cat,TaskPtr&&task) override;

private:
    //localQueueのpop、stealを行う。
    void run();

    //全てのたまっているtaskを処理し、終了
    void stop();

    void DebugLog(JobCategory cat);

    std::string jobCategoryToString(JobCategory cat);

private:
    //localQのインデックス
    const size_t workerId;
    JobStats& stats_;
    WorkerPolicy policy_;

    std::thread     thread_;
    std::atomic<bool> running = false;

    //タスクキュー
    std::unique_ptr<RealTimeTaskStorage>realTimeTaskStorage;
    std::unique_ptr<BackGroundTaskStorage>backGroundTaskStorage;

    LocalQPtr localQueue;

    LocalQPtr stealQueues;
};

template<typename WorkerPolicy, typename ExecuteFunc>
inline Worker<WorkerPolicy, ExecuteFunc>::Worker(size_t id,LocalQPtr queues,JobStats&stats) : workerId(id), stats_(stats),localQueue(queues[id]), stealQueues(queues),running(true),thread_([this]{run();})
{
    realTimeTaskStorage = std::make_unique<RealTimeTaskStorage>();
    backGroundTaskStorage = std::make_unique<BackGroundTaskStorage>();
}

template<typename WorkerPolicy,typename ExecuteFunc>
inline Worker<WorkerPolicy, ExecuteFunc>::~Worker()
{
    stop();
        
    if (thread_.joinable()) {
        thread_.join();
    }
}

template<typename WorkerPolicy, typename ExecuteFunc>
inline TaskPtr Worker<WorkerPolicy, ExecuteFunc>::schedule(JobCategory cat, Job&& job, int degree)
{

    //カウント
    stats_.onEnqueued(cat,1);
    switch (cat)
    {
    case JobCategory::RealTime:
        return realTimeTaskStorage->pushOne(std::move(job), degree, cat);
    case JobCategory::BackGround:
        return backGroundTaskStorage->pushOne(std::move(job), degree, cat);
    }

    return nullptr;
}

template<typename WorkerPolicy,typename ExecuteFunc>
inline TaskPtr Worker<WorkerPolicy, ExecuteFunc>::schedule(JobCategory cat,Job&& job, int degree, std::vector<TaskPtr>&deps)
{
    //deps処理

    //カウント
    stats_.onEnqueued(cat,1);
    switch (cat)
    {
        case JobCategory::RealTime:
            return realTimeTaskStorage->pushOne(std::move(job),degree,cat);
        case JobCategory::BackGround:
            return backGroundTaskStorage->pushOne(std::move(job), degree, cat);
    }

    return nullptr;
}

template<typename WorkerPolicy, typename ExecuteFunc>
inline TaskPtr Worker<WorkerPolicy, ExecuteFunc>::schedule(JobCategory cat, TaskPtr&& task)
{
    //カウント
    stats_.onEnqueued(cat,1);

    switch (cat)
    {
    case JobCategory::RealTime:
        return realTimeTaskStorage->pushOne(std::move(task));
    case JobCategory::BackGround:
        return backGroundTaskStorage->pushOne(std::move(task));
    }

    return nullptr;
}

//template<typename ChunkAllocator, typename WorkerPolicy>
//inline bool Worker<ChunkAllocator, WorkerPolicy>::popOrSteal(ChunkHandle* chunkHandle)
//{
//    Chunk chunk;
//    //リアルタイムJobを処理
//    //自キューからPOP
//    {
//        auto popRes = localQueue->popQueue(chunk);
//
//        if (popRes == PopStatus::Success) {
//            //jobExecuter->runChunk(std::move(chunk));
//            return true;
//        }
//        else if (popRes == PopStatus::WouldBlock) {
//            std::this_thread::yield();
//            return true;
//        }
//    }
//
//    //他キューからSteal
//    {
//        auto result = localQueue->stealQueues(chunk, stealQueues);
//
//        if (result == StealStatus::Success) {
//            //jobExecuter->runChunk(std::move(chunk));
//            return true;
//        }
//    }
//
//    std::this_thread::yield();
//    return false
// 
//}

template<typename WorkerPolicy, typename ExecuteFunc>
inline void Worker<WorkerPolicy,ExecuteFunc>::run()
{
    while (running.load(std::memory_order_relaxed)) {

        SliceChunk slice;
        if(policy_(localQueue,stealQueues,realTimeTaskStorage, backGroundTaskStorage, slice)){
            //取得失敗なので次のループへ
            if(slice.count == 0) continue;

            JobCategory cat = slice.owner->tryGetPtr(slice.start)->category;
            size_t count = slice.count;

            //Chunk実行
            stats_.onDequeued(cat, count);
            stats_.onStart(cat, count);
            ExecuteFunc(workerId,std::move(slice));
            stats_.onFinish(cat, count);

            //ログ出力
            DebugLog(cat);
        }
    }
}

template<typename WorkerPolicy, typename ExecuteFunc>
inline void Worker<WorkerPolicy,ExecuteFunc>::stop()
{
    running.store(false, std::memory_order_relaxed);

    while(stats_.notCompletedJobCount(JobCategory::RealTime) > 0 && stats_.notCompletedJobCount(JobCategory::BackGround) > 0){
        SliceChunk slice;

        if (policy_(localQueue, stealQueues, realTimeTaskStorage, backGroundTaskStorage, slice)) {
            if (slice.count == 0) continue;

            JobCategory cat = slice.owner->tryGetPtr(slice.start)->category;
            size_t count = slice.count;

            //Chunk実行
            stats_.onDequeued(cat, count);
            stats_.onStart(cat, count);
            ExecuteFunc(workerId, std::move(slice));
            stats_.onFinish(cat, count);

            //ログ出力
            DebugLog(cat);
        }
    }
}

template<typename WorkerPolicy, typename ExecuteFunc>
inline void Worker<WorkerPolicy,ExecuteFunc>::DebugLog(JobCategory cat)
{
    static std::mutex logMutex;

    std::lock_guard<std::mutex>(logMutex);

    auto notCompletedCount = stats_.notCompletedJobCount();
    test::saveLog("[FINISH] queue=%zu outstanding=%zu", workerId, notCompletedCount);
    std::printf("[FINISH] queue=%zu outstanding=%zu \n", workerId, notCompletedCount);

    if(notCompletedCount == 0){
        test::saveLog("All FINISH : %s", jobCategoryToString(cat));
        std::printf("All FINISH : %s", jobCategoryToString(cat));
    }
}

template<typename WorkerPolicy, typename ExecuteFunc>
inline std::string Worker<WorkerPolicy,ExecuteFunc>::jobCategoryToString(JobCategory cat)
{
    switch (cat) {
    case JobCategory::RealTime:   return "RealTime";
    case JobCategory::BackGround: return "BackGround";
    default:    return "Unknown";
    }
}

}