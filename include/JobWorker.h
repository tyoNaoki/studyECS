#pragma once
#include "JobDeque.hpp"
#include "taskPtr.hpp"
#include "TaskQueue.h"
#include "JobBarrier.h"
#include "ThreadSafeQueue.h"

#include "third_party\moodycamel\concurrentqueue.h"

namespace ECS::JobSystem{

class JobManager;

    struct Logger {
        inline static thread_local std::ostringstream localLogBuffer;
        inline static std::mutex outMutex;

        template<typename... Args>
        static void log(const Args&... args) {
            (localLogBuffer << ... << args) << "\n";
        }

        static void flushLogs() {
            std::lock_guard<std::mutex> lk(outMutex);
            std::cout << localLogBuffer.str();
            localLogBuffer.str("");
            localLogBuffer.clear();
        }
    };

struct RealTimePolicy{

    template<typename LocalQ>
    bool operator()(JobCategory&executeCategory,size_t workerId,LocalQ& localQ)
    {
        /*if((*localQ)->isAbort()){
            return false;
        }*/

        auto& manager = JobManager::Instance();
        
        auto& stats = manager.getStats();

        // realTimeJob処理
        if (stats.scheduledJobCount(JobCategory::RealTime) >  0){

            ChunkMeta chunkHandle;

            if(localQ.try_dequeue(chunkHandle)){
                manager.executor().runChunk(workerId, std::move(chunkHandle));
                executeCategory = JobCategory::RealTime;

                return true;
            }else if(manager.getFlushChunk(chunkHandle)){
                manager.executor().runChunk(workerId, std::move(chunkHandle));
                executeCategory = JobCategory::RealTime;
                return true;
            }
        }

        return false;
    }

    //数値を必ず2の累乗
    //static constexpr size_t localQueueCap = 1'6384;
    static constexpr size_t localQueueCap = 32;
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

struct JobEntry;

class IWorker {
public:
    virtual ~IWorker() = default;

    virtual void enqueue(JobCategory jobCategory, ChunkMeta&& chunk) = 0;

    //BatchJob
    virtual void enqueue(JobCategory jobCategory, Job&& job) = 0;
};

template<
    typename LocalQueue,
    typename WorkerPolicy = RealTimePolicy
    >
class Worker : public IWorker{
    //using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;

    //using LocalQPtr = std::unique_ptr<LocalQueue>*;

public:
    static constexpr size_t localQueueCapacity = WorkerPolicy::localQueueCap;
    
    static constexpr size_t maxBatchSize = 32;
    //static constexpr size_t maxBatchSize = 2048;

    //static constexpr size_t realTimeCap = 20'000;
    //static constexpr size_t backGroundCap = 1'000;

    ////512
    //static constexpr size_t realTimeChunkSize = 64;
    //static constexpr size_t backGroundChunkSize = 512;

    //static constexpr size_t slotWorkCapacity = 32;

    //localQやtaskQの初期化処理など
    Worker(size_t id,JobBarrier& barrier, moodycamel::ConcurrentQueue<ChunkMeta>&conCurrentQueue);

    //残っているtaskの処理
    ~Worker() override;
   
    //BackGround未完成
    void enqueue(JobCategory jobCategory,ChunkMeta&& chunk) override;

    //BatchJob
    void enqueue(JobCategory jobCategory, Job&&job) override;

private:
    //localQueueのpop、stealを行う。
    void run();

    //全てのたまっているtaskを処理し、終了
    void stop();

    void DebugLog(JobCategory cat);

    std::string jobCategoryToString(JobCategory cat);

private:
    std::atomic<bool> running = false;

    //localQのインデックス
    const size_t workerId;
    WorkerPolicy policy_;

    //TaskArena taskStorage;

    moodycamel::ConcurrentQueue<ChunkMeta>&queue;
    moodycamel::ProducerToken token;

    //LocalQPtr localQueue;

    //LocalQPtr stealQueues;

    //size_t stealQueueSize;

    std::thread     thread_;
};

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::Worker(size_t id,JobBarrier&barrier, moodycamel::ConcurrentQueue<ChunkMeta>& conCurrentQueue) : workerId(id),running(true),queue(conCurrentQueue),token(conCurrentQueue),
    thread_([this,&barrier]{
        barrier.wait();
        run();})
{
}

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::~Worker()
{
    stop();
        
    if (thread_.joinable()) {
        thread_.join();
    }
}

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::enqueue(JobCategory jobCategory, ChunkMeta&& chunk)
{
    switch (jobCategory)
    {
    case ECS::JobSystem::JobCategory::RealTime:
        queue.enqueue(token,std::move(chunk));
        break;
    case ECS::JobSystem::JobCategory::BackGround:
        ASSERT(false,"not work");
        return;
        break;
    default:
        return;
        break;
    }
}

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::enqueue(JobCategory jobCategory, Job&& job)
{
    switch (jobCategory)
    {
    case ECS::JobSystem::JobCategory::RealTime:
        ASSERT(false, "not work");
        //taskStorage.enqueue(std::move(job));
        break;
    case ECS::JobSystem::JobCategory::BackGround:
        ASSERT(false, "not work");
        return;
        break;
    default:
        return;
        break;
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::run()
{
    JobCategory category;
    while (running.load(std::memory_order_relaxed)) {
        if(policy_(category,workerId,queue)){
            ASSERT(size_t(category) < size_t(JobCategory::Num), "JobCategroy is falid num");

            //ログ出力
            //DebugLog(category);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::stop()
{
    running.store(false, std::memory_order_relaxed);

    JobCategory category;

    auto& stats = JobManager::Instance().getStats();
    while(stats.scheduledJobCount(JobCategory::RealTime) > 0 && stats.scheduledJobCount(JobCategory::BackGround) > 0){

        //bool operator()(JobCategory&executeCategory,size_t workerId,LocalQ& localQ, StealQs& stealQs,size_t queueSize,RealTimeTasks& rt, BackGroundTasks& bg)
        if (policy_(category,workerId,queue)) {
            ASSERT(size_t(category) < size_t(JobCategory::Num), "JobCategroy is falid num");
            //ログ出力
            DebugLog(category);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::DebugLog(JobCategory cat)
{
    auto&jm = JobManager::Instance();

    const auto& stats = jm.getStats();

    ASSERT(size_t(cat) < size_t(JobCategory::Num), "JobCategroy is falid num");
    auto notCompletedCount = stats.scheduledJobCount(cat);

    test::saveLog("[FINISH] queue=%zu outstanding=%zu", workerId, notCompletedCount);
    //Logger::log("[FINISH] queue =",workerId,", outstanding =",notCompletedCount);

    std::printf("[FINISH] queue=%zu outstanding=%zu \n", workerId, notCompletedCount);

    if(notCompletedCount == 0){
        test::saveLog("All FINISH : %s", jobCategoryToString(cat).c_str());
        //Logger::log("All FINISH : ", jobCategoryToString(cat).c_str());

        std::printf("All FINISH : %s\n", jobCategoryToString(cat).c_str());
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline std::string Worker<LocalQueue,WorkerPolicy>::jobCategoryToString(JobCategory cat)
{
    switch (cat) {
    case JobCategory::RealTime:   return "RealTime";
    case JobCategory::BackGround: return "BackGround";
    default:    return "Unknown";
    }
}

}