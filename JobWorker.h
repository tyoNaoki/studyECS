#pragma once
#include "JobDeque.hpp"

namespace ECS::JobSystem{

    using TaskPtr = intrusive_ptr<Task>;

    struct JobExecutor {

        void operator()(size_t workerId, SliceChunk&& chunk) {
            runChunk(workerId, std::move(chunk));
        }

    private:
        void runJob(size_t workerId, TaskPtr& task) {
            ASSERT(task && task->job.valid(), "task is invoked in JobQueue!!");

            task->job.invoke();

            //繋がっているchildの依存カウントを減らしていく
            for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
                if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    //pushJobWaitQueue(child);
                }
            }
        }

        void runChunk(size_t workerId, SliceChunk&& chunk) {
            ASSERT(chunk.count > 0, "chunkPtr is empty!!");

            auto start = chunk.start;
            auto end = chunk.count;

            for (size_t i = start; i < end; i++) {
                auto task = chunk.owner->getRef(i);

                ASSERT(task, "task is nullptr");
                runJob(workerId, task);

                //デストラクタ呼び出し
                chunk.owner->destroyAt(i);
            }
        }
    };

inline void runJob(size_t workerId, TaskPtr& task) {
    ASSERT(task && task->job.valid(), "task is invoked in JobQueue!!");

    task->job.invoke();

    //繋がっているchildの依存カウントを減らしていく
    for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
        if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            //pushJobWaitQueue(child);
        }
    }
}

inline void runChunk(size_t workerId, SliceChunk&& chunk) {
    //ASSERT(chunk.count > 0, "chunkPtr is empty!!");

    auto owner = chunk.owner;
    size_t start = chunk.start;
    size_t end = start + chunk.count;

    // インライン化可能なら完全にインライン化
    for (size_t i = start; i < end; ++i) {
        auto task = owner->getRef(i);
        //ASSERT(task, "task is nullptr");

        runJob(workerId, task);          // inline化 or バッチ化
        owner->destroyAt(i);             // バルク破棄検討
    }
}

struct RealTimePolicy{

    template<typename LocalQ, typename StealQs, typename RealTimeTasks, typename BackGroundTasks, typename Chunk>
    bool operator()(LocalQ& localQ, StealQs& stealQs,size_t queueSize,RealTimeTasks& rt, BackGroundTasks& bg, Chunk& chunkHandle)
    {
        if((*localQ)->isAbort()){
            return false;
        }

        Chunk chunk;
        auto& manager = JobManager::Instance();

        size_t takeNum = (*localQ)->remainingTasks();

        //スロットが空ではない
        if((*localQ)->remainingSlot() != 0){
            //待機キューから取得
            //popmanyからstd::vectorをこぴーしてもらう
            std::vector<Chunk> chunks = rt->popMany(takeNum);

            //配列から取り出す
            while (!chunks.empty()) {
                Chunk chunk = std::move(chunks.back());

                chunks.pop_back();

                (*localQ)->pushWithTimeout(std::move(chunk), rt);
            }
        }
        
        auto realTimeJobCount = manager.getPendingJobCount(JobCategory::RealTime);
        //realTimeJob処理
        if (realTimeJobCount >  0 && (*localQ)->popOrSteal(stealQs, queueSize,chunkHandle)) return true;

        takeNum = (*localQ)->remainingTasks();

        if ((*localQ)->remainingSlot() != 0) {
            //待機キューから取得
            std::vector<Chunk> chunks = bg->popMany(takeNum);

            while (!chunks.empty()) {
                auto chunk = std::move(chunks.back());

                chunks.pop_back();

                (*localQ)->pushWithTimeout(std::move(chunk), bg);
            }
        }
        
        //backGroundJob処理
        if (manager.getPendingJobCount(JobCategory::BackGround) <= 0) return false;

        return (*localQ)->popOrSteal(stealQs,queueSize,chunkHandle);
    }

    static constexpr size_t realTimeCap = 20'000;
    static constexpr size_t backGroundCap = 1'000;

    //512
    static constexpr size_t realTimeChunkSize = 1024;
    static constexpr size_t backGroundChunkSize = 512;

    //1024
    static constexpr size_t localQueueSlotCap = 16;
    static constexpr size_t localQueueMaxTask = 16384;
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
    typename LocalQueue,
    typename WorkerPolicy = RealTimePolicy,
    typename ExecuteFunc = JobExecutor
    >
class Worker : public IWorker{
    //using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;

    using LocalQPtr = std::unique_ptr<LocalQueue>*;

    using RealTimeTaskStorage = RealTimeStorageType<WorkerPolicy>;

    using BackGroundTaskStorage = BackGroundStorageType<WorkerPolicy>;

public:
    static constexpr std::size_t localQueueSlotCap = WorkerPolicy::localQueueSlotCap;
    static constexpr std::size_t localQueueMaxTask = WorkerPolicy::localQueueMaxTask;

    //localQやtaskQの初期化処理など
    Worker(size_t id,LocalQPtr queues,size_t queueSize,JobStats&stats,JobBarrier& barrier);

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

    size_t stealQueueSize;
};

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::Worker(size_t id,LocalQPtr queues,size_t queueSize,JobStats&stats,JobBarrier&barrier) : workerId(id), stats_(stats),localQueue(&queues[id]), stealQueues(queues),stealQueueSize(queueSize),running(true),thread_([this,&barrier]{barrier.wait();run();})
{
    realTimeTaskStorage = std::make_unique<RealTimeTaskStorage>();
    backGroundTaskStorage = std::make_unique<BackGroundTaskStorage>();
}

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::~Worker()
{
    stop();
        
    if (thread_.joinable()) {
        thread_.join();
    }
}

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline TaskPtr Worker<LocalQueue,WorkerPolicy,ExecuteFunc>::schedule(JobCategory cat, Job&& job, int degree)
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

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline TaskPtr Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::schedule(JobCategory cat,Job&& job, int degree, std::vector<TaskPtr>&deps)
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

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline TaskPtr Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::schedule(JobCategory cat, TaskPtr&& task)
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

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline void Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::run()
{
    while (running.load(std::memory_order_relaxed)) {

        SliceChunk slice;

        if(policy_(localQueue,stealQueues, stealQueueSize,realTimeTaskStorage, backGroundTaskStorage, slice)){
            //取得失敗なので次のループへ
            if(slice.count == 0) continue;

            JobCategory cat = slice.owner->get(slice.start)->category;
            size_t count = slice.count;

            //Chunk実行
            stats_.onDequeued(cat, count);
            stats_.onStart(cat, count);
            ExecuteFunc{}(workerId,std::move(slice));
            stats_.onFinish(cat, count);

            //ログ出力
            DebugLog(cat);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline void Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::stop()
{
    running.store(false, std::memory_order_relaxed);

    while(stats_.notCompletedJobCount(JobCategory::RealTime) > 0 && stats_.notCompletedJobCount(JobCategory::BackGround) > 0){
        SliceChunk slice;

        if (policy_(localQueue, stealQueues, stealQueueSize,realTimeTaskStorage, backGroundTaskStorage, slice)) {
            if (slice.count == 0) continue;

            auto task = slice.owner->get(slice.start);
            JobCategory cat = task->category;
            size_t count = slice.count;

            //Chunk実行
            stats_.onDequeued(cat, count);

            stats_.onStart(cat, count);
            ExecuteFunc{}(workerId,std::move(slice));
            stats_.onFinish(cat, count);

            //ログ出力
            DebugLog(cat);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline void Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::DebugLog(JobCategory cat)
{
    static std::mutex logMutex;

    std::lock_guard<std::mutex>log(logMutex);

    auto notCompletedCount = stats_.notCompletedJobCount(cat);
    if(notCompletedCount > 90'000){
        auto pendingC = stats_.pendingJobCount(JobCategory::RealTime);
        auto runningC = stats_.runningJobCount(JobCategory::RealTime);
        std::printf("pending is %zu,running is %zu",pendingC,runningC);
    }

    test::saveLog("[FINISH] queue=%zu outstanding=%zu", workerId, notCompletedCount);
    std::printf("[FINISH] queue=%zu outstanding=%zu \n", workerId, notCompletedCount);

    if(notCompletedCount == 0){
        test::saveLog("All FINISH : %s", jobCategoryToString(cat).c_str());
        std::printf("All FINISH : %s\n", jobCategoryToString(cat).c_str());
    }
}

template<typename LocalQueue,typename WorkerPolicy,typename ExecuteFunc>
inline std::string Worker<LocalQueue,WorkerPolicy, ExecuteFunc>::jobCategoryToString(JobCategory cat)
{
    switch (cat) {
    case JobCategory::RealTime:   return "RealTime";
    case JobCategory::BackGround: return "BackGround";
    default:    return "Unknown";
    }
}

}