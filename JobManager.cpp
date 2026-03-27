#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"
#include "JobWorker.h"

namespace ECS::JobSystem{

void ECS::JobSystem::JobManager::Executor::runJob(size_t workerId, JobId* Id) {
    ASSERT(*Id, "task is invoked in JobQueue!!");

    JobManager& jm = JobManager::Instance();
    auto& jobEntry = jm.getJobEntry(*Id);        
    //ASSERT(jobEntry.job.valid(), "this job is executed");

    //jobEntry.job.invoke();
    //jobEntry.inner->setReady(true);

    //processDependents(jobEntry.data);

    jm.stats_.onJobFinish(jobEntry.jobCategory,1);
}

void ECS::JobSystem::JobManager::Executor::runSlot(size_t workerId, JobId*begin, JobId*end)
{
    if (begin == end) return;
    JobManager& jm = JobManager::Instance();

    auto jobCategory = jm.getJobEntry(*begin).jobCategory;
    size_t size = end - begin;

    //slot実行
    for (auto it = begin; it != end; ++it) {
        ASSERT(jm.containsJob(*it), "job not contains");
        
        auto& job = jm.getJob(*it);
        ASSERT(job.valid(), "this job is executed");

        job.invoke();

        //processDependents(job.data);
    }

    //カウント減算
    jm.stats_.onJobFinish(jobCategory, size);
}

void ECS::JobSystem::JobManager::Executor::runChunk(size_t workerId, ChunkMeta&& chunk)
{
    if(chunk.isEmpty()) return;

    JobManager& jm = JobManager::Instance();

    size_t size = chunk.size();

    //chunk実行
    for (size_t i = 0; i < size; i++) {
        chunk.jobs[i].invoke();
    }

    /*for (auto it = chunk.jobs.begin(); it != chunk.jobs.end(); ++it) {
        ASSERT(it->valid(), "this job is executed");

        it->invoke();
    }*/

    //カウント減算
    jm.stats_.onJobFinish(chunk.jobCategory, size);
}

void JobManager::Initialize(size_t reserveJobNum,size_t threadCount, std::unique_ptr<TimelineRecorder> rec)
{
    ASSERT(threadCount > 0, "JobSystem is ThreadCount <= 0");
    initFlag = true;

    recorder = std::move(rec);
    stopFlag = false;
    nextQueue = 0;
    threadSize = threadCount;

    barrier.init(1);

    localQueues.reserve(threadCount);
    workers.reserve(threadCount);

    for (size_t i = 0; i < threadCount; ++i) {
        localQueues.emplace_back(std::make_unique<JobQueue>(RealTimeOnlyWorker::localQueueCapacity, i));
        workers.emplace_back(std::make_unique<RealTimeOnlyWorker>(i,localQueues.data(),localQueues.size(),barrier));
    }

    jobStorage.reserveJobs(reserveJobNum);
}

JobManager::~JobManager()
{
    if (!initFlag)return;
}

JobHandle JobManager::scheduleJobHandle(std::shared_ptr<Inner>inner,TaskCategory taskCategory,JobCategory jobCategory,JobId jobId,Job&& job)
{
    //いずれ、自動でtaskCategoryを算出できるようにする
    stats_.onScheduled(jobCategory, 1);

    //いずれ、これら二つの変数を詰め込んだjobだけをchunkにつめていく。

    //jobをJobManagerにコピー
    // 
    //ここでindegree追加するか処置

     //jobとdependents,inner
    auto index = getJobIndex(jobId);
    //auto& func = jobStorage.getFunc(index);
    //func = std::move(job);
    auto& jobInfo = jobStorage.getJobInfo(index);
    jobInfo.jobCategory = jobCategory;
    jobInfo.taskCategory = taskCategory;
    jobStorage.getDependents(index).clear();

    enqueue(taskCategory, jobCategory, std::move(job));

    auto handle = JobHandle::createHandle(jobId, std::move(inner));

    return handle;
}

//JobHandle JobManager::scheduleJobHandle(JobId jobId, JobHandle& handle)
//{
//    auto& entry = getJobEntry(jobId);
//
//    ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");
//
//    //いずれ、自動でtaskCategoryを算出できるようにする
//    stats_.onScheduled(entry.jobCategory, 1);
//    entry.inner = std::make_shared<Inner>();
//
//    addDependent(jobId, handle);
//
//    if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
//        enqueue(entry.taskCategory, entry.jobCategory, jobId);
//    }else{
//          //待機ジョブに登録
//size_t waitJobIndex = jobStorage.emplaceWaitJob();
//auto& entry = jobStorage.getWaitJob(waitJobIndex);
//entry.jobCategory = jobCategory;
//entry.taskCategory = taskCategory;
//  
//      }
//
//    return JobHandle::createHandle(jobId, entry.inner);
//}

//JobHandle JobManager::scheduleJobHandle(JobId jobId, std::vector<JobHandle>& jobHandles)
//{
//    auto& entry = getJobEntry(jobId);
//
//    ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");
//
//    //いずれ、自動でtaskCategoryを算出できるようにする
//    stats_.onScheduled(entry.jobCategory, 1);
//    entry.inner = std::make_shared<Inner>();
//
//    for (auto& parent : jobHandles) {
//        addDependent(jobId, parent.jobId);
//    }
//
//    if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
//        enqueue(entry.taskCategory, entry.jobCategory, jobId);
//    }
//
//    return JobHandle::createHandle(jobId, entry.inner);
//}
//
//JobHandle JobManager::scheduleJobHandle(JobId jobId, Job&& job, std::vector<JobHandle>& jobHandles)
//{
//    auto& entry = getJobEntry(jobId);
//
//    ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");
//
//    //いずれ、自動でtaskCategoryを算出できるようにする
//    stats_.onScheduled(entry.jobCategory, 1);
//    entry.inner = std::make_shared<Inner>();
//    entry.job = std::move(job);
//
//    for (auto& parent : jobHandles) {
//        addDependent(jobId, parent.jobId);
//    }
//
//    if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
//        enqueue(entry.taskCategory, entry.jobCategory, jobId);
//    }
//
//    return JobHandle::createHandle(jobId, entry.inner);
//}

bool JobManager::checkRanAllJobInJobQueues()
{
    for (auto& queue : localQueues) {
        if (queue->validCheck()) {
            return false;
        }
    }

    return true;
}

void JobManager::removeJob(JobId& jobId)
{
    jobStorage.removeJob(jobId);
    jobId = NULL_JOB_ID;
}

//TaskPtr JobManager::pushJobWaitQueue(Job&& job, int degree, JobCategory cat)
//{
//    auto task = new Task(job, 0, cat);
//    TaskPtr taskPtr{ std::move(task) };
//
//    switch (cat)
//    {
//    case JobCategory::RealTime:
//        pushRealTimeJobWaitQueue(std::move(taskPtr));
//        break;
//    case JobCategory::BackGround:
//        pushBackGroudGlobalQueue(std::move(taskPtr));
//        break;
//    default:
//        break;
//    }
//}


void JobManager::popGlobalBackGroundQueue() {
    //if (globalBackGroudQueue.empty()) return; // グローバルキューが空の場合終了

    //auto startTime = getStartFrameTime();
    /*auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();*/

    auto chunkClamp = std::make_pair(1,100);

    //処理する全て
    //size_t popBGNum = calculatePOPBGJobs(frameTimeMs, elapsed_ms,avg_ExecuteJobTime);

    //if(popBGNum <= 0) return;// 処理時間が残っていない

    //ASSERT(popBGNum <= globalBackGroudQueue.size(), "do not upper than globalQueue.Size()");

    //size_t rawJobs = popBGNum / getThreadSize(); //一つのワーカーの処理するジョブ数

    //backGroundCounter.fetch_add(popBGNum, std::memory_order_acq_rel);

    //繰り上げ分入れるために最後を除いたワーカー分、まずはpopする
    size_t workerNum = getThreadSize() - 1;

    ASSERT(false,"BackGroundJOb not work");

    //for (size_t t = 0; t < workerNum; ++t) {
    //    //chunk
    //    for(int j = 0; j < rawJobs;j++){
    //        if (auto task = try_popGlobalBackGroundQueue()) {
    //            // グローバルから取り出して各待機キューに割り当て
    //            workers[t]->schedule(JobCategory::BackGround,std::move(*task));
    //        }
    //        else {
    //            return; // グローバルキューが空になった場合終了
    //        }
    //    }
    //}

    // 最後の分は繰り上げ分入れる
    //auto lastPopNum = popBGNum - (rawJobs * workerNum);
    //
    //// 最後だけ
    //for(int i = 0;i < lastPopNum;i++){
    //    // chunk分
    //    if (auto task = try_popGlobalBackGroundQueue()) {
    //        // グローバルから取り出す
    //        workers[workerNum]->schedule(JobCategory::BackGround, std::move(*task));
    //    }
    //    else {
    //        return; // グローバルキューが空になった場合終了
    //    }
    //}   
}

bool JobManager::stealChunk(const JobCategory category, ChunkMeta& chunk)
{
    for(auto& localQ : localQueues){
        auto result = localQ->stealTop(99);

        if(result.first == StealStatus::Success){
            chunk = std::move(*result.second);
            return true;
        }
    }
    
    return false;
}

void JobManager::processDependents(const JobId& parentId)
{
    if(!containsJob(parentId)) return;

    auto&parentDependents = getDependents(getJobIndex(parentId));

    for(auto& child : parentDependents){
        auto& childJobEntry = getJobEntry(child);

        //auto& childJob = childJobEntry.data;

        if (childJobEntry.inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            //スケジュール
            scheduleDependentHandle(child);
        }
    }

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

void JobManager::enqueue(TaskCategory taskCategory,JobCategory jobCategory, Job&&job){
    //chunkとしてまとめてから後で
    if(taskCategory == TaskCategory::Batch){
        size_t queueIndex = getNextQueueIndex();
        workers[queueIndex]->enqueue(jobCategory, std::move(job));
        return;
    }

    ChunkMeta chunk = ChunkMeta();
    chunk.jobs.emplace_back(std::move(job));
    chunk.jobCategory = jobCategory;

    size_t queueIndex = getNextQueueIndex();
    workers[queueIndex]->enqueue(jobCategory, std::move(chunk));
}

void JobManager::enqueue(JobCategory jobCategory, std::vector<Job>&& jobs){
    ChunkMeta chunk = ChunkMeta(jobs.size());

    std::swap(chunk.jobs,jobs);
    chunk.jobCategory = jobCategory;

    size_t queueIndex = getNextQueueIndex();
    workers[queueIndex]->enqueue(jobCategory, std::move(chunk));
}

bool JobManager::allQueuesEmpty() const
{
    for (auto& dq : localQueues)
        if (!dq->empty()) return false;
    return true;
}

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
    return nextQueue.fetch_add(1, std::memory_order_relaxed) % localQueues.size();
}

size_t JobManager::calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime) {
    ASSERT(avgJobTime > 0.0, "calculateBGJobs avgJobTime <= 0.0");

    // 残り時間
    double remaining_ms = target_ms - elapsed_ms;
    if (remaining_ms <= safetyMarginMs||remaining_ms <= avgJobTime) return 0.0; // 十分な残り時間がない場合終了

    /*return static_cast<size_t>(std::min(std::floor(remaining_ms / avgJobTime), static_cast<double>(globalBackGroudQueue.size())));*/

    return 0;
}

void JobManager::addDependent(const JobId& child,const JobId& parent)
{
    ASSERT(child != parent, "addDependent() do not use childJob and childJob");

    jobStorage.addDependent(child,parent);
}

void JobManager::addDependent(const JobId& child, JobHandle& parent)
{
    ASSERT(child != parent.getJobId(), "addDependent() do not use childJob and childJob");

    jobStorage.addDependent(child, parent.getJobId());
}

void JobStats::onJobFinish(const JobCategory cat, size_t count) noexcept
{
    bool shouldNotify = false;
    {
        auto& counter = scheduled[size_t(cat)];
        if (counter.fetch_sub(count, std::memory_order_acq_rel) == count) {
            // 0 になった瞬間
            shouldNotify = true;
        }
    }
    if (shouldNotify) {
        cv_.notify_all();
    }
}

void JobStats::waitForAll(const JobCategory cat)
{
    std::unique_lock<std::mutex> lk(mtx_);
    auto& jm = JobManager::Instance();

    while (true) {
        if (scheduledJobCount(cat) == 0) {
            break;
        }

        cv_.wait(lk);
    }
}

bool JobHandle::isComplete() const
{
    ////実行可否
    return inner->isReady();
}

void JobHandle::Complete() const
{
    auto& jm = JobManager::Instance();
    auto& job = jm.getJobEntry(jobId);

    ////実行完了するまでChunkをフラッシュしてChunk実行し続ける
    while (!inner->isReady()) {

        ChunkMeta chunk;

        //ワーカースレッドからstealする
        if (jm.stealChunk(job.jobCategory, chunk)) {
            //chunk実行
            jm.executor().runChunk(99, std::move(chunk));
            continue;
        }

        //何も取れない
        std::this_thread::yield();
    }
}

bool CombineJobHandles::isComplete() const
{
    return inner->isReady();
}

void CombineJobHandles::Complete() const
{
    auto& jm = JobManager::Instance();
    auto& job = jm.getJobEntry(jobIds[0]);

    ////実行完了するまでChunkをフラッシュしてChunk実行し続ける
    while (!inner->isReady()) {

        ChunkMeta chunk;

        //ワーカースレッドからstealする
        if (jm.stealChunk(job.jobCategory, chunk)) {
            //chunk実行
            jm.executor().runChunk(99, std::move(chunk));
            continue;
        }

        //何も取れない
        std::this_thread::yield();
    }
}

}//namespace ECS::JobSystem