#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"
#include "JobWorker.h"

namespace ECS::JobSystem{

void ECS::JobSystem::JobManager::Executor::runJob(size_t workerId, JobId* Id) {
    ASSERT(*Id, "task is invoked in JobQueue!!");

    JobManager& jm = JobManager::Instance();
    auto& jobEntry = jm.getJobEntry(*Id);        
    ASSERT(jobEntry.job.valid(), "this job is executed");

    jobEntry.job.invoke();
    jobEntry.inner->setReady(true);

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
    for (JobId* it = begin; it != end; ++it) {
        ASSERT(jm.containsJob(*it), "job not contains");
        auto& job = jm.getJobEntry(*it);

        ASSERT(job.job.valid(), "this job is executed");

        job.job.invoke();
        job.inner->setReady(true);

        //processDependents(job.data);
    }

    //カウント減算
    jm.stats_.onJobFinish(jobCategory, size);
}

void ECS::JobSystem::JobManager::Executor::runChunk(size_t workerId, ChunkMeta&& chunk)
{
    if(chunk.begin == chunk.end) return;

    JobManager& jm = JobManager::Instance();

    size_t size = chunk.size();

    //chunk実行
    for (JobId* it = chunk.begin; it != chunk.end; ++it) {
        ASSERT(jm.containsJob(*it),"job not contains");
        
        auto& job = jm.getJobEntry(*it);
        ASSERT(!job.inner->isReady(),"this job is executed");
        ASSERT(job.job.valid(), "this job is executed");
        
        job.job.invoke();
        job.inner->setReady(true);

        //依存関係の解決
        processDependents(job);
    }

    //カウント減算
    jm.stats_.onJobFinish(chunk.getJobCategory(), size);
}

void ECS::JobSystem::JobManager::Executor::processDependents(JobEntry& parent)
{
    auto& jm = JobManager::Instance();

    for(auto& child : parent.dependents){
        auto& childJobEntry = jm.getJobEntry(child);

        //auto& childJob = childJobEntry.data;

        if (childJobEntry.inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            //スケジュール済み
            if (childJobEntry.inner) {
                jm.scheduleDependentHandle(child);
            }
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

    taskStorage.Initialize(JobCategory::RealTime, slotWorkCapacity, realTimeCap, realTimeChunkSize);

    jobStorage.reserveJobs(reserveJobNum);
}

JobManager::~JobManager()
{
    if (!initFlag)return;
}

JobHandle JobManager::scheduleJobHandle(JobId jobId,Job&& job)
{
    auto& entry = getJobEntry(jobId);

    ASSERT(!entry.inner || entry.inner->isReady(), "this job is scheduled");

    //いずれ、自動でtaskCategoryを算出できるようにする
    stats_.onScheduled(entry.jobCategory, 1);

    //いずれ、これら二つの変数を詰め込んだjobだけをchunkにつめていく。
    auto inner = std::make_shared<Inner>();
    entry.job = std::move(job);

    if (entry.inDegree.load(std::memory_order_acquire) == 0) {
        enqueue(entry.taskCategory, entry.jobCategory, jobId);
    }

    auto handle = JobHandle::createHandle(jobId, std::move(inner));
    entry.inner = handle.inner;

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
//    }
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

void JobManager::addRemoveJob(JobId& jobId)
{
    jobStorage.addRemoveJob(jobId);
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

//frame最後の同期ポイントで必ず行う

void JobManager::removeJobsOnLastFrame()
{
    ASSERT(!clearTaskStorage(JobCategory::RealTime), "Some Job can not executed");

    //削除予定のjobDataをここで一斉に削除
    jobStorage.removeJobs();
}

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

void JobManager::allFlushJob(const JobCategory category){
    //全フラッシュ
    //全てのJobをchunkにつめていく
    taskStorage.flushIncomplete();

    popChunks();
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

void JobManager::getFlushChunk(const JobCategory category, ChunkMeta& chunk)
{
    taskStorage.flushIncomplete();

    if(!chunk.isEmpty()){
        auto&job = getJobEntry(*chunk.begin);
        if(job.inner->isReady()){
            ASSERT(false,"inner is true");
        }
    }
    popChunk(chunk);
}

void JobManager::enqueue(TaskCategory taskCategory,JobCategory jobCategory, JobId jobId){
    taskStorage.enqueue(taskCategory, jobId);

    popChunks();
}

void JobManager::popChunks()
{
    if(taskStorage.isEmptyChunks()) return;

    ChunkMeta chunkMeta;

    //空になるまでqueueに割り振る
    //各taskStorage毎に行う
    while(taskStorage.popOne(chunkMeta)){

        size_t queueIndex = getNextQueueIndex();
        workers[queueIndex]->enqueue(chunkMeta.getJobCategory(),std::move(chunkMeta));
    }
}

void JobManager::popChunk(ChunkMeta& chunk)
{
    if (taskStorage.isEmptyChunks()) return;

    taskStorage.popOne(chunk);
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

bool JobManager::clearTaskStorage(JobCategory category)
{
    //いずれ各カテゴリー毎に実装
    switch (category)
    {
    case JobCategory::RealTime:
        return taskStorage.clearAllJobHandles();
    default:
        break;
    }

    return false;
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
        if (!jm.stealChunk(job.jobCategory, chunk)) {
            //グローバルキューにあるjobをフラッシュする
            jm.getFlushChunk(job.jobCategory, chunk);
        }

        //chunkが空ではない
        if (!chunk.isEmpty()) {
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
        if (!jm.stealChunk(job.jobCategory, chunk)) {
            //グローバルキューにあるjobをフラッシュする
            jm.getFlushChunk(job.jobCategory, chunk);
        }

        //chunkが空ではない
        if (!chunk.isEmpty()) {
            //chunk実行
            jm.executor().runChunk(99, std::move(chunk));
            continue;
        }

        //何も取れない
        std::this_thread::yield();
    }
}

}//namespace ECS::JobSystem