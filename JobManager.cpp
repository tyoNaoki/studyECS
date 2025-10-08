#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"

namespace ECS::JobSystem{

void ECS::JobSystem::JobManager::Executor::runJob(size_t workerId, JobHandle* handle) {
    ASSERT(handle, "task is invoked in JobQueue!!");

    JobManager& jm = JobManager::Instance();
    auto& jobEntry = jm.getJobEntry(handle->jobId);
    //ASSERT(job.func,"job is executed");

    jobEntry.func(jobEntry.data);
    jobEntry.func = nullptr;

    //依存ジョブがある場合
    //if (job->nextDependent != std::nullopt) {
    //    //依存カウンタをリンク順にたどって減算していく。
    //    processDependents(job);
    //}

    jm.stats_.onJobFinish(handle->jobCategory,1);
}

void ECS::JobSystem::JobManager::Executor::runSlot(TaskArena* owner, size_t workerId, size_t offset, size_t localIndex)
{
    auto* begin = owner->getJobsBeginInSlot(offset, localIndex);
    auto* end = owner->getJobsEndInSlot(offset, localIndex);

    if (begin == end) return;

    JobManager& jm = JobManager::Instance();

    for (auto* it = begin; it != end; ++it) { 
        auto& job = jm.getJobEntry(it->jobId);

        //ASSERT(job.func,"job is executed");

        job.func(job.data);
        job.func = nullptr;
    }

    //依存ジョブがある場合
        //if(job->nextDependent!=std::nullopt){
        //    //依存カウンタをリンク順にたどって減算していく。
        //    processDependents(job);
        //}

    jm.stats_.onJobFinish(begin->jobCategory,owner->getSizeInSlot(offset,localIndex));
}

void ECS::JobSystem::JobManager::Executor::runChunk(size_t workerId, ChunkMeta&& chunk)
{
    for (size_t i = 0; i < chunk.size; i++) {
        runSlot(chunk.owner, workerId, chunk.offset, i);
    }
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

void JobManager::Initialize(size_t threadCount, std::unique_ptr<TimelineRecorder> rec)
{
    ASSERT(threadCount > 0, "JobSystem is ThreadCount <= 0");
    initFlag = true;

    recorder = std::move(rec);
    stopFlag = false;
    nextQueue = 0;
    threadSize = threadCount;
    
    //初期化
    /*for (size_t i = 0; i < threadCount; ++i) {
       
    }*/

    barrier.init(1);

    localQueues.reserve(threadCount);
    workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        localQueues.emplace_back(std::make_unique<JobQueue>(RealTimeOnlyWorker::localQueueMaxChunk, i));
        workers.emplace_back(std::make_unique<RealTimeOnlyWorker>(i,localQueues.data(),localQueues.size(),barrier));
    }
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