#pragma once
#include "JobDeque.hpp"
#include "taskPtr.hpp"
#include "TaskQueue.h"
#include "JobBarrier.h"
#include "ThreadSafeQueue.h"

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

    template<typename LocalQ, typename StealQs,typename TaskQueue>
    bool operator()(JobCategory&executeCategory,size_t workerId,LocalQ& localQ, StealQs& stealQs,size_t queueSize,TaskQueue& taskQueue)
    {
        if((*localQ)->isAbort()){
            return false;
        }

        auto& manager = JobManager::Instance();

        size_t takeNum = (*localQ)->emptyNum();

        // 空きスロットがある
        if((*localQ)->emptyNum() != 0){
            // 待機キューから取得
            std::vector<ChunkMeta> chunks;

            // RealTime待機キューから取得
            taskQueue.pop(chunks);

            while (!chunks.empty()) {

                ChunkMeta chunk = std::move(chunks.back());

                chunks.pop_back();
                // localQueueにpushする。
                // 制限時間以上ロックされていた場合、待機キューに戻す
                (*localQ)->pushWithTimeout(std::move(chunk), taskQueue);
            }
        }
        
        auto& stats = manager.getStats();

        // realTimeJob処理
        if (stats.scheduledJobCount(JobCategory::RealTime) >  0){

            ChunkMeta chunkHandle;

            if((*localQ)->popOrSteal(stealQs, queueSize, chunkHandle)){

                //取得失敗
                if (chunkHandle.isEmpty()) return false;

                //chunk実行
                manager.executor().runChunk(workerId, std::move(chunkHandle));
                executeCategory = JobCategory::RealTime;

                return true;
            }else{
                //未完成のchunkを一つ取得、実行
                manager.getFlushChunk(JobCategory::RealTime,chunkHandle);

                if(!chunkHandle.isEmpty()){

                    //chunk実行
                    manager.executor().runChunk(workerId, std::move(chunkHandle));
                    executeCategory = JobCategory::RealTime;

                    return true;
                }
            }
        }

        return false;
    }

    static constexpr size_t localQueueCap = 32;
};
//
//struct TaskChunk {
//    std::vector<Job>jobs;
//    std::vector<std::vector<size_t>>dependents;
//
//    std::vector<std::shared_ptr<Inner>>inners;
//};
//
//struct TaskStorage {
//    void addDependent(size_t child, size_t parent) {
//        auto& childJob = getJobInfo(child);
//
//        //std::lock_guard<std::mutex> lk(dependentLocks[sparse[getJobIndex(parent)]]);
//
//        if (getFunc(parent).valid()) { // まだ実行されていない
//            //childを親のnextDependentに差し込む
//            getDependents(parent).push_back(child);
//
//            //子ジョブの未解決依存数を増やす
//            childJob.inDegree.fetch_add(1, std::memory_order_relaxed);
//        }
//    }
//
//    void reserveJobs(size_t reserveCount) {
//        sparse.reserve(reserveCount);
//        jobIds.reserve(reserveCount);
//    }
//
//    void clearAll() {
//        std::lock_guard<std::mutex> lk(lock);
//
//        sparse.clear();
//        waitQueue.clear();
//        dependents.clear();
//        //dependentLocks.clear();
//        jobIds.clear();
//        freeIds.clear();
//        removeJobData.clear();
//        nextIndex = 0;
//    }
//
//    //JobIDを返す
//    JobId emplaceJobId() {
//        std::lock_guard<std::mutex> lk(lock);
//
//        JobId newId = allocateJobId();
//
//        if (sparse.size() <= getJobIndex(newId)) {
//            sparse.emplace_back();
//            sparse.back() = NULL_JOB_ID;
//        }
//
//        ASSERT(sparse[getJobIndex(newId)] == NULL_JOB_ID, "valid sparse slot do not use");
//        jobIds.push_back(newId);
//
//        return newId;
//    }
//
//    size_t emplaceJob(JobId newId) {
//        ASSERT(containsJob(newId), "not contains job");
//
//        auto result = jobs.size();
//
//        jobInfos.emplace_back();
//        jobs.emplace_back();
//        dependents.emplace_back();
//        inners.emplace_back(nullptr);
//
//        sparse[getJobIndex(newId)] = result;
//
//        return result;
//    }
//
//    void pushBackWaitQueue(size_t jobIndex) {
//        std::lock_guard<std::mutex> lk(lock);
//
//        waitQueue.push_back(jobIndex);
//    }
//
//    size_t getDenseIndex(const JobId jobId) {
//        ASSERT(containsJob(jobId), "not contains JobId");
//
//        return sparse[getJobIndex(jobId)];
//    }
//
//    JobId& getWaitJob(const size_t jobIndex) {
//        return waitQueue[jobIndex];
//    }
//
//    JobEntry& getJobInfo(size_t index) {
//        return jobInfos[index];
//    }
//
//    Job& getFunc(size_t index) {
//        return jobs[index];
//    }
//
//    std::vector<JobId>& getDependents(size_t index) {
//        return dependents[index];
//    }
//
//    std::shared_ptr<Inner>& getInner(size_t index) {
//        return inners[index];
//    }
//
//    //schedule時に対応ジョブに関数ポインターを割り当てる
//    /*template<class DerivedJob>
//    void createJobFunction(const JobId& id){
//        jobData[getJobIndex(id)].func = &Invoke<DerivedJob>;
//    }*/
//
//    void addRemoveJob(JobId jodId) { removeJobData.push_back(jodId); }
//
//    void addRemoveJobs(std::vector<JobId>&& jobs) {
//        removeJobData.insert(removeJobData.end(),
//            std::move_iterator(jobs.begin()),
//            std::move_iterator(jobs.end()));
//    }
//
//    //GetJobEntry関数の参照が壊れるので、絶対にFrameの最後全てのジョブを処理か、処理をしていないタイミングで行うこと!!
//    void removeJobs() {
//        std::lock_guard<std::mutex> lk(lock);
//
//        std::sort(removeJobData.begin(), removeJobData.end(),
//            [&](JobId& a, JobId& b) {
//                return sparse[getJobIndex(a)] > sparse[getJobIndex(b)]; // removeIndex の大きい順
//            });
//
//        for (auto& removeId : removeJobData) {
//            //removeJob(removeId);
//        }
//
//        removeJobData.clear();
//    }
//
//    bool containsJob(const JobId id) const {
//        JobIndex index = getJobIndex(id);
//        return index < sparse.size() && jobIds[index] == id;
//    }
//
//    //template<class T>
//    //static void Invoke(IJobBase* raw) {
//    //    //static_cast<T*>(raw)->Execute();
//    //    //raw->Execute();
//    //}
//
//    JobId allocateJobId() {
//        if (!freeIds.empty()) {
//            // removeの時以外でfreeIdsが使用されないので、ロックレスで問題なし
//            JobId old = freeIds.back();
//            freeIds.pop_back();
//
//            return composeJobId(getJobIndex(old), getJobVersion(old) + 1);
//        }
//
//        return composeJobId(nextIndex++, 0u);
//    }
//
//private:
//    void removeWaitJob(size_t removeJobIndex) {
//        /*auto& waitJob = waitJobs.back();
//        auto rastJobIndex = getJobIndex(waitJob.jobId);
//        auto swapId = waitJobs[removeJobIndex].jobId;
//
//        sparse[rastJobIndex] = removeJobIndex;
//        sparse[swapId] = NULL_JOB_INDEX;
//
//        std::swap(waitJobs[removeJobIndex],waitJobs.back());
//        waitJobs.pop_back();*/
//    }
//
//    void swap(size_t job, size_t job2) {
//        std::swap(jobInfos[job], jobInfos[job2]);
//        std::swap(jobs[job], jobs[job2]);
//        std::swap(dependents[job], dependents[job2]);
//        std::swap(inners[job], inners[job2]);
//    }
//
//    //void removeJob(JobId& id) {
//    //    JobId removeId = jobIds[getJobIndex(id)];
//    //    JobIndex removeIndex = getJobIndex(removeId);
//
//    //    std::lock_guard<std::mutex> lk(lock);
//
//    //    //すでに無効
//    //    ASSERT(jobData.empty() || removeIndex >= jobData.size(),"this id is NULL");
//
//    //    jobIds.back() = removeId;
//
//    //    std::swap(jobData[removeIndex], jobData.back());
//    //    std::swap(jobIds[removeIndex], jobIds.back());
//
//    //    jobData.pop_back();
//    //    jobIds.pop_back();
//    //    //dependentLocks.pop_back();
//
//    //    sparse[id] = NULL_JOB_INDEX;
//    //    freeIds.push_back(id);
//    //    id = NULL_JOB_ID;
//    //}
//
//private:
//    
//    std::vector<size_t>readyQueue;
//
//    std::vector<JobEntry>jobInfos;
//    std::vector<Job>jobs;
//    std::vector<std::vector<size_t>>dependents;
//
//    std::vector<std::shared_ptr<Inner>>inners;
//    //std::vector<std::mutex> dependentLocks;
//
//    std::mutex lock;
//};

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
};

template<
    typename LocalQueue,
    typename WorkerPolicy = RealTimePolicy
    >
class Worker : public IWorker{
    //using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;

    using LocalQPtr = std::unique_ptr<LocalQueue>*;

public:
    static constexpr size_t localQueueCapacity = WorkerPolicy::localQueueCap;

    //static constexpr size_t realTimeCap = 20'000;
    //static constexpr size_t backGroundCap = 1'000;

    ////512
    //static constexpr size_t realTimeChunkSize = 64;
    //static constexpr size_t backGroundChunkSize = 512;

    //static constexpr size_t slotWorkCapacity = 32;

    //localQやtaskQの初期化処理など
    Worker(size_t id,LocalQPtr queues,size_t queueSize,JobBarrier& barrier);

    //残っているtaskの処理
    ~Worker() override;
   
    //BackGround未完成
    void enqueue(JobCategory jobCategory,ChunkMeta&& chunk) override;

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

    ThreadSafeQueue taskQueue;

    LocalQPtr localQueue;

    LocalQPtr stealQueues;

    size_t stealQueueSize;

    std::thread     thread_;
};

template<typename LocalQueue,typename WorkerPolicy>
inline Worker<LocalQueue,WorkerPolicy>::Worker(size_t id,LocalQPtr queues,size_t queueSize,JobBarrier&barrier) : workerId(id),localQueue(&queues[id]), stealQueues(queues),stealQueueSize(queueSize),running(true), 
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
        taskQueue.push(std::move(chunk));
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

//template<typename LocalQueue, typename WorkerPolicy>
//inline void Worker<LocalQueue, WorkerPolicy>::enqueue(ChunkMeta* chunk)
//{
//    switch (chunk->getJobCategory())
//    {
//    case ECS::JobSystem::JobCategory::RealTime:
//        chunkQueue.push(chunk);
//        break;
//    case ECS::JobSystem::JobCategory::BackGround:
//        ASSERT(false,"not work");
//        return;
//        break;
//    default:
//        return;
//        break;
//    }
//
//}

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
        if(policy_(category,workerId,localQueue,stealQueues, stealQueueSize, taskQueue)){
            ASSERT(size_t(category) < size_t(JobCategory::Num), "JobCategroy is falid num");

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
        if (policy_(category,workerId,localQueue, stealQueues, stealQueueSize,taskQueue)) {
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