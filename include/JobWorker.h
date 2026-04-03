#pragma once
#include "JobDeque.hpp"
#include "taskPtr.hpp"
#include "TaskQueue.h"
#include "JobBarrier.h"
#include "ThreadSafeQueue.h"

#include "moodycamel\concurrentqueue.h"

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

struct GeneralPolicy{

    template<typename LocalQ>
    bool operator()(size_t workerId,LocalQ& localQ)
    {
        /*if((*localQ)->isAbort()){
            return false;
        }*/

        auto& manager = JobManager::Instance();
        
        auto& stats = manager.getStats();

        // realTimeJob処理
        if (stats.scheduledJobCount() >  0){

            ChunkMeta chunkHandle;

            if(localQ.try_dequeue(chunkHandle)){
                manager.executor().runChunk(workerId, std::move(chunkHandle));

                return true;
            }else if(manager.getFlushChunk(chunkHandle)){
                manager.executor().runChunk(workerId, std::move(chunkHandle));
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

    virtual void enqueue(ChunkMeta&& chunk) = 0;

    virtual void completedJob(JobId jobId) = 0;

};

template<
    typename LocalQueue,
    typename WorkerPolicy = GeneralPolicy
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
    Worker(size_t id,JobBarrier& barrier, moodycamel::ConcurrentQueue<ChunkMeta>&chunkQueue, moodycamel::ConcurrentQueue<JobId>&completedJobQueue);

    //残っているtaskの処理
    ~Worker() override;
   
    //BackGround未完成
    void enqueue(ChunkMeta&& chunk) override;

    void completedJob(JobId jobId) override;

private:
    //localQueueのpop、stealを行う。
    void run();

    //全てのたまっているtaskを処理し、終了
    void stop();

    void DebugLog();

private:
    std::atomic<bool> running = false;

    //localQのインデックス
    const size_t workerId;
    WorkerPolicy policy_;

    //TaskArena taskStorage;

    moodycamel::ConcurrentQueue<ChunkMeta>& chunkQueue;
    moodycamel::ProducerToken chunkToken;
    moodycamel::ConcurrentQueue<JobId>& completedJobQueue;
    moodycamel::ProducerToken completeJobtoken;

    //LocalQPtr localQueue;

    //LocalQPtr stealQueues;

    //size_t stealQueueSize;

    std::thread     thread_;
};

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::Worker(size_t id,JobBarrier&barrier, moodycamel::ConcurrentQueue<ChunkMeta>& chunkQ, moodycamel::ConcurrentQueue<JobId>& completedJobQ) : workerId(id),running(true), chunkQueue(chunkQ),chunkToken(chunkQ), completedJobQueue(completedJobQ),completeJobtoken(completedJobQ),
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
inline void Worker<LocalQueue, WorkerPolicy>::enqueue(ChunkMeta&& chunk)
{
    chunkQueue.enqueue(chunkToken, std::move(chunk));
}

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::completedJob(JobId jobId)
{
    completedJobQueue.enqueue(completeJobtoken,jobId);
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::run()
{
    while (running.load(std::memory_order_relaxed)) {
        if(policy_(workerId,chunkQueue)){

            //ログ出力
            //DebugLog(category);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::stop()
{
    running.store(false, std::memory_order_relaxed);

    auto& stats = JobManager::Instance().getStats();
    while(stats.scheduledJobCount() > 0){

        //bool operator()(JobCategory&executeCategory,size_t workerId,LocalQ& localQ, StealQs& stealQs,size_t queueSize,RealTimeTasks& rt, BackGroundTasks& bg)
        if (policy_(workerId,chunkQueue)) {
            //ログ出力
            DebugLog();
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::DebugLog()
{
    auto&jm = JobManager::Instance();

    const auto& stats = jm.getStats();

    auto notCompletedCount = stats.scheduledJobCount();

    test::saveLog("[FINISH] queue=%zu outstanding=%zu", workerId, notCompletedCount);
    //Logger::log("[FINISH] queue =",workerId,", outstanding =",notCompletedCount);

    std::printf("[FINISH] queue=%zu outstanding=%zu \n", workerId, notCompletedCount);

    if(notCompletedCount == 0){
        test::saveLog("All FINISH JOB");
        //Logger::log("All FINISH : ", jobCategoryToString(cat).c_str());

        std::printf("All FINISH JOB\n");
    }
}

}