#pragma once
#include "JobDeque.hpp"
#include "ChunkAllocator.h"

namespace ECS::JobSystem{

//class JobExecutor {
//public:
//    void execute(size_t queueIndex,Chunk&& chunk) {
//        runChunk(queueIndex,chunk);
//    }
//
//private:
//    void runJob(size_t queueIndex,TaskPtr& task) {
//        ASSERT(task && task->job.valid(), "task is invoked in JobQueue!!");
//
//        task->job.invoke();
//
//        //繋がっているchildの依存カウントを減らしていく
//        for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
//            if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
//                pushJobWaitQueue(child);
//            }
//        }
//
//        static std::mutex logMutex;
//
//        // runJob 内
//        auto prev = outstanding.fetch_sub(1, std::memory_order_acq_rel);
//        bool didAllFinish = (prev == 1);
//
//        if (didAllFinish) {
//            std::lock_guard<std::mutex> lk(finishMutex);
//            finishCv.notify_all();
//        }
//
//        {
//            std::lock_guard<std::mutex> lk2(logMutex);
//            if (didAllFinish) {
//                test::saveLog("[All FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
//                std::printf("[All FINISH] queue=%zu outstanding=%zu \n", queueIndex, outstanding.load());
//            }
//            else {
//                test::saveLog("[FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
//                std::printf("[FINISH] queue=%zu outstanding=%zu \n", queueIndex, outstanding.load());
//            }
//        }
//    }
//
//    void runChunk(size_t queueIndex,Chunk& chunk) {
//        ASSERT(chunk, "chunkPtr is nullptr!!");
//
//        while (true) {
//            auto idx = chunk->start.fetch_add(1, std::memory_order_acq_rel);
//            auto end = chunk->count.load(std::memory_order_acquire);
//
//            if (idx >= end)
//                break; // 全部終わった
//
//            auto task = std::move(chunk->tasks[idx]);
//            runJob(queueIndex, std::move(task));
//        }
//    }
//};

struct RealTimePolicy{

    template<typename LocalQ,typename StealQs,typename RealQ,typename BgQ,typename Chunk>
    bool operator()(LocalQ&localQ, StealQs& stealQs,RealQ&rtQ,BgQ&bgQ, Chunk& chunkHandle)
    {
        if(localQ.isAbort()){
            return false;
        }

        Chunk chunk;

        //待機キューから取得
        while(true){
            if(!rtQ.try_pop(chunk)||!localQ.pushWithTimeout(chunk,rtQ)) break;
        }

        //realTimeJob処理
        if (rtQ->getTaskCounter() != 0 && localQ->popOrSteal(stealQs,chunkHandle)) return true;

        //realTimeJobのpopもstealも失敗した場合
        
        //backGroundJob処理
        if(bgQ->getTaskCounter() == 0) return false;
        
        while (true) {
            if (!bgQ.try_pop(chunk) || !localQ.pushWithTimeout(chunk, bgQ)) break;
        }
        
        return localQ->popOrSteal(stealQs,chunkHandle);
    }

    static constexpr size_t realTimeCap = 20'000;
    static constexpr size_t backGroundCap = 1'000;

    static constexpr size_t localQueueCap = 1024;
};

struct BackGroundPolicy {

    template<typename LocalQ,typename StealQs,typename RealQ, typename BgQ, typename Chunk>
    bool operator()(LocalQ& localQ, StealQs& stealQs,RealQ& rtQ, BgQ& bgQ, Chunk& chunkHandle)
    {
        if (localQ.isAbort()) {
            return false;
        }

        Chunk chunk;

        //待機キューから取得
        while (true) {
            if (!bgQ.try_pop(chunk) || !localQ.pushWithTimeout(chunk, bgQ)) break;
        }

        //backGroundJob処理
        if (bgQ->getTaskCounter() != 0 && localQ->popOrSteal(stealQs,chunkHandle)) return true;

        //backGroundJobのpopもstealも失敗した場合

        //realTimeJob処理
        if (rtQ->getTaskCounter() == 0) return false;

        while (true) {
            if (!rtQ.try_pop(chunk) || !localQ.pushWithTimeout(chunk, rtQ)) break;
        }

        return localQ->popOrSteal(stealQs,chunkHandle);
    }

    static constexpr size_t realTimeCap = 20'000;
    static constexpr size_t backGroundCap = 1'000;

    static constexpr size_t localQueueCap = 1024;
};

template<
    typename ChunkAllocator,
    typename WorkerPolicy = RealTimePolicy
    >
class Worker {
    static constexpr std::size_t realTimeCap = WorkerPolicy::realTimeCap;
    static constexpr std::size_t backGroundCap = WorkerPolicy::backGroundCap;
    static constexpr std::size_t localQueueCap = WorkerPolicy::localQueueCap;

    using Chunk = ChunkAllocator::Chunk;

    using ChunkHandle = ChunkAllocator::ChunkHandle;

    using JobQueue = Debug::DebugJobQueue<JobDeque<Chunk>>;

    using LocalQPtr = std::unique_ptr<JobQueue>*;

    //タスクキュー
    using RealTaskQueue = TaskQueue<TaskPtr, realTimeCap, ChunkAllocator>;
    using BackGroundTaskQueue = TaskQueue<TaskPtr, backGroundCap, ChunkAllocator>;

public:
    Worker(size_t id,ChunkAllocator* allocator,LocalQPtr queues);

    ~Worker();

    bool pushJob(Job&&job);

    //bool popOrSteal(ChunkHandle* chunkHandle);

private:
    run();

    stop();

private:
    const size_t workerId;
    std::thread     thread_;
    std::atomic<bool> running = false;

    //タスクキュー
    std::unique_ptr<RealTaskQueue>realTimeTaskQueue;
    std::unique_ptr<BackGroundTaskQueue>backGroundTaskQueue;

    LocalQPtr localQueue;

    LocalQPtr stealQueues;

    ChunkAllocator* allocator_;
};

template<typename ChunkAllocator, typename WorkerPolicy>
inline Worker<ChunkAllocator, WorkerPolicy>::Worker(size_t id, ChunkAllocator* allocator,LocalQPtr queues) : workerId(id), allocator_(allocator), localQueue(queues[id]), stealQueues(queues),running(true),thread_([this]{run();})
{
    realTimeTaskQueue = std::make_unique<RealTaskQueue>(allocator.get());
    backGroundTaskQueue = std::make_unique<BackGroundTaskQueue>(allocator.get());
}

template<typename ChunkAllocator, typename WorkerPolicy>
inline Worker<ChunkAllocator, WorkerPolicy>::~Worker()
{
    stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

template<typename ChunkAllocator, typename WorkerPolicy>
inline bool Worker<ChunkAllocator, WorkerPolicy>::pushJob(Job&& job)
{
    return false;
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
//    return false;
//}

template<typename ChunkAllocator, typename WorkerPolicy>
inline Worker<ChunkAllocator, WorkerPolicy>::run()
{
    
    ChunkHandle chunkHandle = nullptr;
    while (running_.load(std::memory_order_relaxed)) {

        if(WorkerPolicy(localQueue,stealQueues,realTimeTaskQueue, backGroundTaskQueue, chunkHandle){
            //jobExecuter->runChunk(std::move(chunkHandle));
        }

        chunkHandle = nullptr;
    }
}

template<typename ChunkAllocator, typename WorkerPolicy>
inline Worker<ChunkAllocator, WorkerPolicy>::stop()
{
    running_.store(false, std::memory_order_relaxed);
}

}