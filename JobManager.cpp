#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"
#include "JobWorker.h"

namespace ECS::JobSystem{

void ECS::JobSystem::JobManager::Executor::runJob(size_t workerId, JobHandle* handle) {
    ASSERT(handle, "task is invoked in JobQueue!!");

    JobManager& jm = JobManager::Instance();
    auto& jobEntry = jm.getJobEntry(handle->jobId);
    ASSERT(jobEntry.status == JobStatus::Scheduled, "job is executed");

    jobEntry.func(jobEntry.data);
    jobEntry.status = JobStatus::Completed;

    processDependents(jobEntry.data);

    jm.stats_.onJobFinish(handle->jobCategory,1);
}

void ECS::JobSystem::JobManager::Executor::runSlot(size_t workerId, JobHandle*begin,JobHandle*end)
{
    if (begin == end) return;
    JobManager& jm = JobManager::Instance();

    auto jobCategory = begin->jobCategory;
    size_t size = end - begin;

    for (JobHandle* it = begin; it != end; ++it) {
        ASSERT(jm.containsJob(it->jobId), "job not contains");
        auto& job = jm.getJobEntry(it->jobId);

        ASSERT(job.status == JobStatus::Scheduled, "job is executed");

        job.func(job.data);
        job.status = JobStatus::Completed;

        processDependents(job.data);
    }

    jm.stats_.onJobFinish(jobCategory, size);
}

void ECS::JobSystem::JobManager::Executor::runChunk(size_t workerId, ChunkMeta&& chunk)
{
    if(chunk.begin == chunk.end) return;
    JobManager& jm = JobManager::Instance();

    auto jobCategory = chunk.begin->jobCategory;
    size_t size = chunk.size();

    for (JobHandle* it = chunk.begin; it != chunk.end; ++it) {
        ASSERT(jm.containsJob(it->jobId),"job not contains");
        auto& job = jm.getJobEntry(it->jobId);

        ASSERT(job.status == JobStatus::Scheduled, "job is executed");

        job.func(job.data);
        job.status = JobStatus::Completed;
        //依存関係の解決
        processDependents(job.data);
    }

    jm.stats_.onJobFinish(jobCategory, size);
}

void ECS::JobSystem::JobManager::Executor::processDependents(IJobBase* parentJob)
{
    auto& jm = JobManager::Instance();

    for (auto child = std::exchange(parentJob->nextDependent, std::nullopt);
        child != std::nullopt;
        child = std::exchange(parentJob->nextDependent, std::nullopt))
    {
        auto& childJobEntry = jm.getJobEntry(child->jobId);
        auto* childJob = childJobEntry.data;

        if (childJob->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            //スケジュール済み
            if (childJobEntry.func) {
                jm.scheduleDependentHandle(*child);
            }
        }
    }
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
        localQueues.emplace_back(std::make_unique<JobQueue>(RealTimeOnlyWorker::localQueueMaxChunk, i));
        workers.emplace_back(std::make_unique<RealTimeOnlyWorker>(i,localQueues.data(),localQueues.size(),barrier));
    }

    jobStorage.reserveJobs(reserveJobNum);
}

JobManager::~JobManager()
{
    if (!initFlag)return;
}

bool JobManager::checkRanAllJobInJobQueues()
{
    for (auto& queue : localQueues) {
        if (queue->validCheck()) {
            return false;
        }
    }

    return true;
}

void JobManager::addRemoveJob(JobHandle& handle)
{
    jobStorage.addRemoveJob(handle);
    handle.jobId = NULL_JOB_ID;
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

void JobManager::setStartFrameTime()
{
    frameStart = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point JobManager::getStartFrameTime() const
{
    return frameStart;
}

void JobManager::removeJobsOnLastFrame()
{
    for(auto&x:workers){
        //falseならまだ未処理のタスクが残っている
        if(!x->clearTaskStorage(JobCategory::RealTime)){
            ASSERT(false,"Some Job can not executed");
            return;
        }
    }

    //削除予定のjobDataをここで一斉に削除
    jobStorage.removeJobs();
}

void JobManager::popGlobalBackGroundQueue() {
    if (globalBackGroudQueue.empty()) return; // グローバルキューが空の場合終了

    auto startTime = getStartFrameTime();
    auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();

    auto chunkClamp = std::make_pair(1,100);
    //処理する全て
    size_t popBGNum = calculatePOPBGJobs(frameTimeMs, elapsed_ms,avg_ExecuteJobTime);

    if(popBGNum <= 0) return;// 処理時間が残っていない

    ASSERT(popBGNum <= globalBackGroudQueue.size(), "do not upper than globalQueue.Size()");

    size_t rawJobs = popBGNum / getThreadSize(); //一つのワーカーの処理するジョブ数

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
    for (int i = 0; i < workers.size(); i++) {
        workers[i]->flush(category);
    }
}

void JobManager::enqueue(JobHandle handle){
    size_t next = getNextQueueIndex();

    workers[next]->enqueue(handle);
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

    return static_cast<size_t>(std::min(std::floor(remaining_ms / avgJobTime), static_cast<double>(globalBackGroudQueue.size())));
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

}//namespace ECS::JobSystem