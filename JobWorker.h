#pragma once
#include "JobDeque.hpp"
#include "taskPtr.hpp"
#include "TaskQueue.h"
#include "JobBarrier.h"

namespace ECS::JobSystem{

struct RealTimePolicy{

    template<typename LocalQ, typename StealQs, typename RealTimeTasks, typename BackGroundTasks>
    bool operator()(JobCategory&executeCategory,size_t workerId,LocalQ& localQ, StealQs& stealQs,size_t queueSize,RealTimeTasks& rt, BackGroundTasks& bg)
    {
        if((*localQ)->isAbort()){
            return false;
        }

        auto& manager = JobManager::Instance();

        ChunkMeta chunkHandle;

        size_t takeNum = (*localQ)->emptyNum();

        // 空きスロットがある
        if((*localQ)->emptyNum() != 0){
            // 待機キューから取得
            std::vector<ChunkMeta> chunks;

            // RealTime待機キューから取得
            rt->popMany(takeNum,chunks);

            while (!chunks.empty()) {
                ChunkMeta chunk = std::move(chunks.back());

                chunks.pop_back();
                // localQueueにpushする。
                // 制限時間以上ロックされていた場合、待機キューに戻す
                (*localQ)->pushWithTimeout(std::move(chunk), rt);
            }
        }
        
        auto& stats = manager.getStats();
        // realTimeJob処理
        if (stats.scheduledJobCount(JobCategory::RealTime) >  0 && (*localQ)->popOrSteal(stealQs, queueSize,chunkHandle)){
            //chunk実行
            manager.executor().runChunk(workerId,std::move(chunkHandle));
            executeCategory = JobCategory::RealTime;
            return true;
        }

        takeNum = (*localQ)->emptyNum();

        //空きスロットがある
        if ((*localQ)->emptyNum() != 0) {
            std::vector<ChunkMeta> chunks;

            // BackGround待機キューから取得
            bg->popMany(takeNum,chunks);

            while (!chunks.empty()) {
                auto chunk = std::move(chunks.back());

                chunks.pop_back();

                (*localQ)->pushWithTimeout(std::move(chunk), bg);
            }
        }
        
        //backGroundJob処理
        if (stats.scheduledJobCount(JobCategory::BackGround) <= 0) return false;

        if((*localQ)->popOrSteal(stealQs,queueSize,chunkHandle)){
            for (size_t i = 0; i < chunkHandle.size; i++) {
                manager.executor().runSlot(chunkHandle.owner, workerId, chunkHandle.offset, i);
            }

            executeCategory = JobCategory::BackGround;
            return true;
        }
    }

    static constexpr size_t realTimeCap = 20'000;
    static constexpr size_t backGroundCap = 1'000;

    //512
    static constexpr size_t realTimeChunkSize = 64;
    static constexpr size_t backGroundChunkSize = 512;

    static constexpr size_t slotWorkCapacity = 32;

    //1024
    static constexpr size_t localQueueMaxChunk = 1024;
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

//タスクキュー
template<typename Policy>
inline auto makeRealTimeStorage() {
    return std::make_unique<TaskArena>(Policy::slotWorkCapacity,Policy::realTimeCap,Policy::realTimeChunkSize);
}

template<typename Policy>
inline auto makeBackGroundStorage() {
    return std::make_unique<TaskArena>(Policy::slotWorkCapacity,Policy::backGroundCap, Policy::backGroundChunkSize);
}

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

    virtual void enqueue(const JobHandle handle) = 0;

    virtual void flush(const JobCategory cat) = 0;
};

template<
    typename LocalQueue,
    typename WorkerPolicy = RealTimePolicy
    >
class Worker : public IWorker{
    //using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;

    using LocalQPtr = std::unique_ptr<LocalQueue>*;

public:
    static constexpr std::size_t slotWorkCapacity = WorkerPolicy::slotWorkCapacity;
    static constexpr std::size_t localQueueMaxChunk = WorkerPolicy::localQueueMaxChunk;

    //localQやtaskQの初期化処理など
    Worker(size_t id,LocalQPtr queues,size_t queueSize,JobBarrier& barrier);

    //残っているtaskの処理
    ~Worker() override;

    TaskPtr schedule(JobCategory cat, Job&& job, int degree) override;

    TaskPtr schedule(JobCategory cat,Job&& job, int degree, std::vector<TaskPtr>&deps) override;

    TaskPtr schedule(JobCategory cat,TaskPtr&&task) override;

    //仮組
    static constexpr size_t rangeJobCap = 16;
    static constexpr size_t capa = WorkerPolicy::realTimeCap;
    static constexpr size_t chunkCapa = WorkerPolicy::realTimeChunkSize;

    struct testRangeJob {
        std::vector<JobHandle>jobs;

        bool isFull() { return jobs.size() == rangeJobCap; }
        void push(JobHandle handle) { jobs.push_back(handle);}
        size_t size() { return jobs.size(); }
    };
    
    struct testSliceChunk{
        size_t start = 0;
        size_t count = 0;
        std::vector<testRangeJob>* ranges = nullptr;

        bool isFull(){
            return count == chunkCapa;
        }

        testSliceChunk(size_t s,size_t c, std::vector<testRangeJob>* r): start(s),count(c),ranges(r){}

        testSliceChunk() = default;
    };

    std::vector<testRangeJob>taskStore;
    std::deque<testSliceChunk>testSliceDeque;

    std::vector<testSliceChunk>localTaskStore;
    std::atomic<size_t>testlocalTaskCount{ 0 };

    std::mutex testLock;

    void testTaskStoreReserve(){
        taskStore.reserve(capa);
        localTaskStore.reserve(localQueueMaxChunk);
    }

    //BackGround未完成
    void enqueue(const JobHandle handle) override;

    void flush(const JobCategory cat) override;

    bool testPOP(size_t takeNum,std::vector<testSliceChunk>&slices);

    void localPush(testSliceChunk chunk){
        localTaskStore.push_back(chunk);
        testlocalTaskCount.fetch_add(chunk.count,std::memory_order_release);
    }

    void localPOP(testSliceChunk& chunk){
        chunk = localTaskStore.back();
        localTaskStore.pop_back();

        testlocalTaskCount.fetch_sub(chunk.count,std::memory_order_release);
    }

    void testRun(){
        while (running.load(std::memory_order_relaxed)) {

            std::vector<testSliceChunk> slices;
            size_t take = localQueueMaxChunk - testlocalTaskCount.load(std::memory_order_acquire);

            //待機キューからchunkを取り出す
            if(testPOP(take,slices)){
                while(!slices.empty()){
                    auto slice = slices.back();
                    slices.pop_back();
                    localPush(slice);
                }
            }

            if (!localTaskStore.empty()) {
                testSliceChunk chunk;
                localPOP(chunk);

                auto& jm = JobManager::Instance();

                //実行
                for(int i = chunk.start;i<chunk.count;i++){
                    testRangeJob& rangeJob = (*chunk.ranges)[i];

                    for (int j = 0; j < rangeJob.size(); j++) {
                        const JobHandle& jobH = rangeJob.jobs[j];
                        IJobBase* job = jm.getJob(jobH.jobIndex);

                        //ResultSlot& result = rangeJob.rStorage->dense[jobH.resultIndex];

                        jm.executor().runJob(workerId,jobH);

                        onJobComplete(job);

                            //if (ResultSlot* result = rangeJob.results[j]) {

                            //    //この部分を改善すれば、さらに短縮可能
                            //    //auto ret = 1;
                            //    //result->set_value(static_cast<void*>(&ret));
                            //}
                    }

                    //stats_.onFinish(JobCategory::RealTime, rangeJob.size());
                }
            }
        }
    }

private:
    //localQueueのpop、stealを行う。
    void run();

    //全てのたまっているtaskを処理し、終了
    void stop();

    void DebugLog(JobCategory cat);

    std::string jobCategoryToString(JobCategory cat);

    void onJobComplete(IJobBase* job);

    void processDependents(IJobBase* parentJob){
        auto& jm = JobManager::Instance();
        for (auto child = std::exchange(parentJob->nextDependent, std::nullopt);
            child != std::nullopt;
            child = std::exchange(parentJob->nextDependent, std::nullopt))
        {
            IJobBase* childJob = jm.getJob(child->jobIndex);

            if (childJob->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                //スケジュール済み
                if (childJob->ready.load(std::memory_order_acquire)) {
                    jm.scheduleDependentHandle(*child);
                }
            }
        }
    }

private:
    //localQのインデックス
    const size_t workerId;
    WorkerPolicy policy_;

    std::thread     thread_;
    std::atomic<bool> running = false;

    //タスクキュー
    std::unique_ptr<TaskArena>realTimeTaskStorage;
          
    std::unique_ptr<TaskArena>backGroundTaskStorage;

    LocalQPtr localQueue;

    LocalQPtr stealQueues;

    size_t stealQueueSize;
};

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::Worker(size_t id,LocalQPtr queues,size_t queueSize,JobBarrier&barrier) : workerId(id),localQueue(&queues[id]), stealQueues(queues),stealQueueSize(queueSize),running(true), 
    thread_([this,&barrier]{
        barrier.wait();
        run();})
{
    realTimeTaskStorage = makeRealTimeStorage<WorkerPolicy>();
    backGroundTaskStorage = makeBackGroundStorage<WorkerPolicy>();
}

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::~Worker()
{
    stop();
        
    if (thread_.joinable()) {
        thread_.join();
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline TaskPtr Worker<LocalQueue,WorkerPolicy>::schedule(JobCategory cat, Job&& job, int degree)
{

    //カウント
    /*stats_.onEnqueued(cat,1);

    switch (cat)
    {
    case JobCategory::RealTime:
        return realTimeTaskStorage->pushOrAppendRangeTask(std::move(job), degree, cat);
    case JobCategory::BackGround:
        return backGroundTaskStorage->pushOrAppendRangeTask(std::move(job), degree, cat);
    }*/

    return nullptr;
}

template<typename LocalQueue,typename WorkerPolicy>
inline TaskPtr Worker<LocalQueue,WorkerPolicy>::schedule(JobCategory cat,Job&& job, int degree, std::vector<TaskPtr>&deps)
{
    //deps処理

   
    /*switch (cat)
    {
        case JobCategory::RealTime:
            return realTimeTaskStorage->pushOrAppendRangeTask(std::move(job),degree,cat);
        case JobCategory::BackGround:
            return backGroundTaskStorage->pushOrAppendRangeTask(std::move(job), degree, cat);
    }*/

    return nullptr;
}

template<typename LocalQueue,typename WorkerPolicy>
inline TaskPtr Worker<LocalQueue,WorkerPolicy>::schedule(JobCategory cat, TaskPtr&& task)
{
    /*switch (cat)
    {
    case JobCategory::RealTime:
        return realTimeTaskStorage->pushOne(std::move(task));
    case JobCategory::BackGround:
        return backGroundTaskStorage->pushOne(std::move(task));
    }*/

    return nullptr;
}

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::enqueue(const JobHandle handle)
{
    switch (handle.jobCategory)
    {
    case ECS::JobSystem::JobCategory::RealTime:
        realTimeTaskStorage->enqueue(handle.taskCategory,handle);
        break;
    case ECS::JobSystem::JobCategory::BackGround:
        ASSERT(false,"not work backGround");
        return;
        break;
    default:
        return;
        break;
    }

    ////RangeTaskStorageが空か現在のRangeTaskが満タン
    //if (taskStore.empty() || taskStore[taskStore.size() - 1].isFull()) {

    //    //RangeTask作成
    //    taskStore.emplace_back();
    //    taskStore[taskStore.size() - 1].push(handle);

    //    //空
    //    if (testSliceDeque.empty()) {
    //        testSliceDeque.emplace_front(0, 0, &taskStore);
    //    }

    //    //満タン
    //    if (testSliceDeque.front().isFull()) {
    //        size_t newStart = testSliceDeque.front().start + testSliceDeque.front().count;
    //        testSliceDeque.emplace_front(newStart, 0, &taskStore);
    //    }

    //    testSliceDeque.front().count++;

    //    return;
    //}

    ////現在のRangeTaskに追加
    //taskStore[taskStore.size() - 1].push(handle);

    //stats_.onStart(JobCategory::RealTime, 1);
}

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::flush(const JobCategory cat)
{
    switch (cat)
    {
    case ECS::JobSystem::JobCategory::RealTime:
        realTimeTaskStorage->flushIncomplete();
        break;
    case ECS::JobSystem::JobCategory::BackGround:
        ASSERT(false, "not work backGround");
        return;
        break;
    default:
        return;
        break;
    }
}

template<typename LocalQueue, typename WorkerPolicy>
inline bool Worker<LocalQueue, WorkerPolicy>::testPOP(size_t takeNum, std::vector<testSliceChunk>& slices)
{
    if (takeNum <= 0) { return false; }

    size_t take = (takeNum / chunkCapa);

    if (take < 1) {
        return false;
    }

    slices.reserve(take);

    for (int i = 0; i < take; i++) {
        if (testSliceDeque.empty()) {
            break;
        }

        auto slice = testSliceDeque.back();
        testSliceDeque.pop_back();
        slices.push_back(slice);
    }

    return !slices.empty();
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

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::run()
{
    JobCategory category;
    while (running.load(std::memory_order_relaxed)) {

        if(policy_(category,workerId,localQueue,stealQueues, stealQueueSize,realTimeTaskStorage, backGroundTaskStorage)){
            //ログ出力
            DebugLog(category);
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
        if (policy_(category,workerId,localQueue, stealQueues, stealQueueSize,realTimeTaskStorage, backGroundTaskStorage)) {
            //ログ出力
            DebugLog(category);
        }
    }
}

template<typename LocalQueue,typename WorkerPolicy>
inline void Worker<LocalQueue,WorkerPolicy>::DebugLog(JobCategory cat)
{
    static std::mutex logMutex;

    std::lock_guard<std::mutex>log(logMutex);

    const auto& stats = JobManager::Instance().getStats();
    auto notCompletedCount = stats.scheduledJobCount(cat);
    if(notCompletedCount > 90'000){
        std::printf("noCompletedCount is %zu", notCompletedCount);
    }

    test::saveLog("[FINISH] queue=%zu outstanding=%zu", workerId, notCompletedCount);
    std::printf("[FINISH] queue=%zu outstanding=%zu \n", workerId, notCompletedCount);

    if(notCompletedCount == 0){
        test::saveLog("All FINISH : %s", jobCategoryToString(cat).c_str());
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

template<typename LocalQueue, typename WorkerPolicy>
inline void Worker<LocalQueue, WorkerPolicy>::onJobComplete(IJobBase* job)
{
    if (job->nextDependent != std::nullopt) {
        //繋がっているchildの依存カウントを減らしていく
        processDependents(job);
    }

    //未スケジュール状態に戻す
    job->ready.store(false);
}

}