#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"
#include "JobWorker.h"

namespace ECS::JobSystem{

void ECS::JobSystem::JobManager::Executor::runChunk(const size_t workerId, ChunkMeta&& chunk)
{
    if(chunk.isEmpty()) return;

    JobManager& jm = JobManager::Instance();

    size_t size = chunk.size();

    //chunk実行
    for (size_t i = 0; i < size; i++) {
        ASSERT(chunk.jobs[i].valid(), "this job is executed");
        chunk.jobs[i].invoke(workerId);
    }
}

void JobManager::initialize(size_t maxJobNum,size_t threadCount, std::unique_ptr<TimelineRecorder> rec)
{
    ASSERT(threadCount > 0, "JobSystem is ThreadCount <= 0");

    recorder = std::move(rec);
    nextQueue = 0;
    threadSize = threadCount;

    barrier.init(1);

    workers.reserve(threadCount);

    for (size_t i = 0; i < threadCount; ++i) {
        workers.push_back(std::make_unique<GeneralWorker>(i,barrier,chunkQueue,completedJobQueue));
    }

    jobStorage.initialize(maxJobNum);

    currentBatchChunk.jobs.reserve(batchmaxchunksize);

    initFlag = true;
    stopFlag = false;
}

JobManager::~JobManager()
{
    if (!initFlag)return;
}

void JobManager::completedJob(const size_t workerId,JobId jobId)
{
    stats_.onJobFinish(1);

    if(workerId == MAIN_THREAD_ID){//メインスレッドID
        completedJobQueue.enqueue(completedJobToken,jobId);
        return;
    }

    workers[workerId]->completedJob(jobId);
}

namespace {
    thread_local std::vector<JobId> reuseBuffer;
}

void JobManager::processDependents(const size_t workerID,const JobId parentId)
{
    if(!containsJob(parentId)) return;

    {
        auto& parentDependents = getDependents(getJobIndex(parentId));
        std::lock_guard<std::mutex> dlk(*parentDependents.dependentLock);

        if (parentDependents.dependents.empty()) return;
        reuseBuffer.swap(parentDependents.dependents);
    }
    
    for(auto& child : reuseBuffer){

        if(!jobStorage.containsJob(child)) continue;

        auto childIndex = getJobIndex(child);

        auto& childJobEntry = jobStorage.getJobInfo(childIndex);

        if (childJobEntry.inDegree.fetch_sub(1, std::memory_order_release) == 1) {
            //スケジュール
            scheduleDependentHandle(workerID, childIndex);
        }
    }

    reuseBuffer.clear();

    //for (auto child = std::exchange(parentJob->nextDependent, std::nullopt);
    //    child != std::nullopt;
    //    child = std::exchange(parentJob->nextDependent, std::nullopt))
    //{
    //    auto& childJobEntry = jm.getJobEntry(child->jobId);
    //    auto* childJob = childJobEntry.data;

    //    if (childJob->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    //        //スケジュール済み
    //        if (childJobEntry.func) {
    //            jm.scheduleDependentHandle(*child);
    //        }
    //    }
    //}
}

bool JobManager::getFlushChunk(ChunkMeta& chunk)
{
    if (currentBatchChunk.isEmpty())return false;

    std::lock_guard<std::mutex> lk(batchMutex);

    if (currentBatchChunk.isEmpty())return false;

    chunk = std::move(currentBatchChunk);
    currentBatchChunk.jobs.reserve(batchmaxchunksize);
    return true;
}

void JobManager::enqueue(TaskCategory taskCategory,Job&&job){
    //chunkとしてまとめてから後で
    //バッチ処理
    if (taskCategory == TaskCategory::Batch) {

        std::lock_guard<std::mutex> lk(batchMutex);

        currentBatchChunk.jobs.push_back(std::move(job));

        if(currentBatchChunk.jobs.size()>=batchmaxchunksize){
            chunkQueue.enqueue(chunkToken,std::move(currentBatchChunk));

            currentBatchChunk.jobs.reserve(batchmaxchunksize);
        }
        
        return;
    }

    ChunkMeta chunk = ChunkMeta();
    chunk.jobs.push_back(std::move(job));

    chunkQueue.enqueue(chunkToken, std::move(chunk));
}

//void JobManager::enqueue(std::vector<Job>&& jobs){
//    ChunkMeta chunk;
//
//    chunk.jobs = std::move(jobs);
//
//    size_t queueIndex = getNextQueueIndex();
//    workers[queueIndex]->enqueue(std::move(chunk));
//}

void JobManager::abort()
{
    std::lock_guard lk(finishMutex);
    if (!abortFlag.load()) {
        abortFlag.store(true, std::memory_order_release);
        finishCv.notify_all();
    }
}

size_t JobManager::getNextQueueIndex()
{
    return nextQueue.fetch_add(1, std::memory_order_relaxed) % workers.size();
}

void JobManager::addDependent(const JobId& child, JobHandle& parent)
{
    ASSERT(child != parent.getJobId(), "addDependent() do not use childJob and childJob");
    ASSERT(containsJob(child),"%zu is not contains",getJobIndex(child));

    if(!parent.isComplete()){
        jobStorage.addDependent(child, parent);
    }
}

void JobManager::scheduleDependentHandle(const size_t workerID, const JobIndex& childIndex)
{
    auto& entry = getJobInfo(childIndex);

    auto& jobs = getFuncs(childIndex);

    //バッチジョブ用
    if (entry.taskCategory == TaskCategory::Batch) {
        std::lock_guard<std::mutex> lk(batchMutex);

        currentBatchChunk.jobs.push_back(std::move(jobs[0]));
        jobs.pop_back();

        if (currentBatchChunk.jobs.size() >= batchmaxchunksize) {
            if(workerID == MAIN_THREAD_ID){//メインスレッドのID
                chunkQueue.enqueue(chunkToken, std::move(currentBatchChunk));
            }else{
                workers[workerID]->enqueue(std::move(currentBatchChunk));
            }

            currentBatchChunk.jobs.reserve(batchmaxchunksize);
        }

        return;
    }else if(entry.taskCategory == TaskCategory::Parallel){//パラレルジョブ用
        std::vector<Job>swapJobs;
        swapJobs.swap(jobs);
        
        if (workerID == MAIN_THREAD_ID) {
            for (int i = 0; i < swapJobs.size(); i++) {
                ChunkMeta chunk = ChunkMeta();
                chunk.jobs.push_back(std::move(swapJobs[i]));
                chunkQueue.enqueue(chunkToken, std::move(chunk));
            }

        }else {
            for (int i = 0; i < swapJobs.size(); i++) {
                ChunkMeta chunk = ChunkMeta();
                chunk.jobs.push_back(std::move(swapJobs[i]));
                workers[workerID]->enqueue(std::move(chunk));
            }
        }

        return;
    }

    //通常ジョブ用
    ChunkMeta chunk = ChunkMeta();

    chunk.jobs.push_back(std::move(jobs[0]));
    jobs.pop_back();

    if (workerID == MAIN_THREAD_ID) {
        chunkQueue.enqueue(chunkToken, std::move(chunk));
        return;
    }

    workers[workerID]->enqueue(std::move(chunk));
}

void JobStats::onJobFinish(size_t count) noexcept
{
    bool shouldNotify = false;
    {
        if (scheduled.fetch_sub(count, std::memory_order_acq_rel) == count) {
            // 0 になった瞬間
            shouldNotify = true;
        }
    }
    if (shouldNotify) {
        cv_.notify_all();
    }
}

void JobStats::waitForAll()
{
    std::unique_lock<std::mutex> lk(mtx_);
    auto& jm = JobManager::Instance();

    while (true) {
        if (scheduledJobCount() == 0) {
            break;
        }

        cv_.wait(lk);
    }
}

void JobHandle::Complete() const
{
    auto& jm = JobManager::Instance();
    auto& job = jm.getJobEntry(jobId);

    if (!jm.isInitialized() || jm.isAbort()) {
        ASSERT(false, "JobManager is not initialized or aborted");

        return;
    }

    ////実行完了するまでChunkをフラッシュしてChunk実行し続ける
    while (!inner->isReady()) {

        ChunkMeta chunk;

        //ワーカースレッドからstealする
        if (jm.stealChunk(chunk)) {
            //chunk実行
            jm.executor().runChunk(JobManager::MAIN_THREAD_ID, std::move(chunk));
            continue;
        }


        //何も取れない
        std::this_thread::yield();
    }
}

}//namespace ECS::JobSystem