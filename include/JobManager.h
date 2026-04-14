#pragma once
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <array>
#include <limits>
#include <utility>

#include "JobRecorder.h"
#include "JobDeque.hpp"
#include "JobBarrier.h"
#include "JobDebugger.h"
#include "TaskQueue.h"
#include "taskPtr.hpp"
#include "HashFunctions.hpp"
#include "JobWorker.h"

#include "moodycamel\concurrentqueue.h"

namespace ECS::JobSystem{

    template<typename T>
    struct TaskFuture;

    template<>
    struct TaskFuture<void>;

    class JobManager;
    struct ChunkMeta;

    enum class JobStatus : uint8_t{
            UnSchedule,   // スケジュール前
            Scheduled,   // 実行待ち
            Running,     // 実行中
            Completed    //実行完了
    };

    struct JobEntry {
        // 依存関係
        std::atomic<int> inDegree{ 0 };

        // カテゴリ
        TaskCategory taskCategory{};

        JobEntry() = default;

        // ムーブコンストラクタ
        JobEntry(JobEntry&& other) noexcept
            : inDegree(other.inDegree.load())
            , taskCategory(other.taskCategory)
        {
        }

        // ムーブ代入
        JobEntry& operator=(JobEntry&& other) noexcept {
            if (this != &other) {
                inDegree.store(other.inDegree.load());
                taskCategory = other.taskCategory;
            }

            return *this;
        }
    };

    struct DependentNode{
        std::vector<JobId>dependents;
        std::unique_ptr<std::mutex> dependentLock;

        //DependentNode() = default;
        DependentNode() : dependentLock(std::make_unique<std::mutex>()) {}

        DependentNode(const DependentNode&) = delete;
        DependentNode& operator=(const DependentNode&) = delete;

        DependentNode(DependentNode&&) noexcept = default;
        DependentNode& operator=(DependentNode&&) noexcept = default;
    };

    struct JobStorage {

        JobStorage(const JobStorage&) = delete;
        JobStorage& operator=(const JobStorage&) = delete;

        JobStorage(JobStorage&&) = default;
        JobStorage& operator=(JobStorage&&) = default;

        JobStorage() = default;
        ~JobStorage() = default;

        void initialize(size_t size) {
            jobStorageMaxSize = size;
            jobInfos.resize(size);

            jobs.resize(size);
            dependents.resize(size);

            jobIds.resize(size, NULL_JOB_ID);
        }

        void reset() {
            std::lock_guard<std::mutex> lk(lock);

            freeIds.clear();
            nextIndex = 0;
        }

        //JobIDを返す
        JobId emplaceJobId() {
            std::lock_guard<std::mutex> lk(lock);

            JobId newId = allocateJobId();

            auto jobIndex = getJobIndex(newId);

            ASSERT(!containsJob(newId),"%zu is used", getJobIndex(newId));

            jobIds[jobIndex] = newId;
            
            return newId;
        }

        JobEntry& getJobInfo(JobIndex index){
            return jobInfos[index];
        }

        std::vector<Job>& getFuncs(JobIndex index){
            return jobs[index];
        }

        DependentNode& getDependents(JobIndex index) {
            return dependents[index];
        }

        void addDependent(JobId child, JobHandle parentHandle) {
            auto parentJobIndex = getJobIndex(parentHandle.getJobId());

            std::lock_guard<std::mutex>dlk(*dependents[parentJobIndex].dependentLock);

            if(parentHandle.isComplete()) return;

            dependents[parentJobIndex].dependents.push_back(child);

            getJobInfo(getJobIndex(child)).inDegree.fetch_add(1, std::memory_order_acq_rel);
        }

        void removeJob(JobId jobId){
            std::lock_guard<std::mutex> lk(lock);

            auto index = getJobIndex(jobId);
            jobIds[index] = NULL_JOB_ID;
            freeIds.push_back(jobId); // ここはメインスレッド専用なのでロック不要
        }

        //cleanUp時に呼ぶ //メインスレッド専用
        void cleanup(moodycamel::ConcurrentQueue<JobId>& completedJobs) {
            
            JobId jobId;
            while (completedJobs.try_dequeue(jobId)) {
                removeJob(jobId);
            }
        }

        bool containsJob(const JobId id) const{
            JobIndex index = getJobIndex(id);
            return jobIds[index] == id;
        }

        JobId allocateJobId() {
            if(!freeIds.empty()){
                // removeの時以外でfreeIdsが使用されないので、ロックレスで問題なし
                JobId old = freeIds.back();
                freeIds.pop_back();

                return composeJobId(getJobIndex(old), getJobVersion(old) + 1);
            }

            auto newId = composeJobId(nextIndex++, 0u);
            
            ASSERT(nextIndex <= jobStorageMaxSize,"over size %zu",jobStorageMaxSize);

            return newId;
        }

    private:

        void swap(JobIndex job, JobIndex job2){
            std::swap(jobInfos[job],jobInfos[job2]);
            std::swap(jobs[job], jobs[job2]);
            std::swap(dependents[job].dependents, dependents[job2].dependents);
        }

    private:
        //関数ポインター,IJobBase*dataが入っている
        std::vector<JobEntry>jobInfos;
        std::vector<std::vector<Job>>jobs;
        std::vector<DependentNode>dependents;

        //JobIdのリスト
        std::vector<JobId>jobIds;

        //std::vector<JobId>removeJobData;

        JobIndex nextIndex{ 0 };
        std::vector<JobId> freeIds;

        std::mutex lock;

        size_t jobStorageMaxSize;
    };

    /// <summary>
    /*scheduled : スケジュールをされ、まだ取得・実行が始まっていないジョブ数*/
    /// </summary>
    class JobStats {
        friend class JobManager;

        void onJobFinish(size_t count) noexcept;

    public:
        void onScheduled(size_t count) noexcept {
            scheduled.fetch_add(count, std::memory_order_release);
        }

        // フレーム終端で指定カテゴリがすべて完了するまで待つ
        void waitForAll();

        size_t scheduledJobCount() const noexcept{
            return scheduled.load(std::memory_order_acquire);
        }

    private:
        // 各状態のカテゴリ別カウンタ
        std::atomic<size_t> scheduled{};

        // フレーム同期用
        std::mutex             mtx_;
        std::condition_variable cv_;
    };

struct JobHandle;

class JobManager
{
    //using JobQueue = Debug::DebugJobQueue<JobDeque<SliceChunk>>;
    using JobQueue = JobDeque;

    using StealResult = JobQueue::StealResult;

    using PopResult = JobQueue::PopResult;

    using PushResult = JobQueue::PushResult;

    using GeneralWorker = Worker<JobQueue,GeneralPolicy>;

    //仮として60FPS
    static constexpr float targetFPS = 60.0f;

    //1フレームあたりの時間
    static constexpr float frameTimeMs = 1000.0f / targetFPS;

    static constexpr double safetyMarginMs = 1.5;

    static constexpr double bgRatioMin = 0.10;

    static constexpr double bgRatioMax = 0.50;

    JobManager() : chunkToken(chunkQueue),completedJobToken(completedJobQueue){}

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    struct Executor {
        void runChunk(const size_t workerId, ChunkMeta&& chunk);
    };

    template<typename Derived>
    struct JobContext {
        std::shared_ptr<Derived>self;
        JobId jobId;
        std::shared_ptr<Inner>setter;

        JobContext(std::shared_ptr<Derived> s,
            JobId Id,
            std::shared_ptr<Inner>set) :self(s), jobId(Id), setter(set) {}
    };

    template<typename Derived>
    struct ParallelJobContext {
        std::shared_ptr<Derived> self;
        JobId jobId;
        size_t total, batchSize, numBatches, chunkBatches;
        std::atomic<size_t> nextBatch{ 0 };
        std::atomic<size_t> taskCounter{ 0 };
        std::shared_ptr<Inner> setter;

        ParallelJobContext(std::shared_ptr<Derived> s,
            JobId Id,
            size_t t, size_t b, size_t n,
            size_t c,
            std::shared_ptr<Inner> set)
            : self(s), jobId(Id), total(t), batchSize(b), numBatches(n), chunkBatches(c),nextBatch(0),taskCounter(numBatches),setter(std::move(set)) {}

        ~ParallelJobContext() {};
    };

public:
    static constexpr size_t MAIN_THREAD_ID = 99;//メインスレッド専用ワーカーID

    static JobManager& Instance() {
        static JobManager manager;
        return manager;
    }

    //実行用
    Executor& executor() {
        static Executor exec;
        return exec;
    }

    JobStats& getStats(){
        return stats_;
    }

    void initialize(size_t maxJobNum,size_t threadCount,
        std::unique_ptr<TimelineRecorder> rec = nullptr);

    ~JobManager();

    void start(){
        if(initFlag){
            barrier.start();
        }
    }

    template<typename Derived>
    JobHandle scheduleJobHandle(std::shared_ptr<Derived>self,TaskCategory taskCategory) {
        stats_.onScheduled(1);

        JobId id = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        using ContextT = JobContext<Derived>;
        auto context = std::make_shared<ContextT>(self, id, setter);

        auto index = getJobIndex(id);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = taskCategory;

        Job task([context](const size_t workerId) {
            executeIJob(context->self.get(), context->jobId, context->setter.get(), workerId);
            });

        enqueue(taskCategory, std::move(task));

        return JobHandle::createHandle(id,std::move(setter));
    }

    template<typename Derived>
    JobHandle scheduleJobHandle(std::shared_ptr<Derived>self,TaskCategory taskCategory,JobHandle& depedentHandle){
        stats_.onScheduled(1);

        JobId id = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        using ContextT = JobContext<Derived>;
        auto context = std::make_shared<ContextT>(self, id, setter);

        auto index = getJobIndex(id);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = taskCategory;

        Job task([context](const size_t workerId) {
            executeIJob(context->self.get(), context->jobId, context->setter.get(), workerId);
            });
    
        addDependent(id, depedentHandle);
    
        if(jobInfo.inDegree.load(std::memory_order_relaxed) == 0){
            enqueue(taskCategory, std::move(task));
        }else{
            auto& func = jobStorage.getFuncs(index);
            func.push_back(std::move(task));
        }

        return JobHandle::createHandle(id, std::move(setter));
    }

    template<typename Derived>
    JobHandle scheduleJobHandle(std::shared_ptr<Derived>self, TaskCategory taskCategory, std::vector<JobHandle>& depedentHandles){
        stats_.onScheduled(1);

        JobId id = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        using ContextT = JobContext<Derived>;
        auto context = std::make_shared<ContextT>(self, id, setter);

        auto index = getJobIndex(id);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = taskCategory;

        Job task([context](const size_t workerId) {
            executeIJob(context->self.get(), context->jobId, context->setter.get(), workerId);
            });

        for (int i = 0; i < depedentHandles.size(); i++) {
            addDependent(id, depedentHandles[i]);
        }

        if (jobInfo.inDegree.load(std::memory_order_relaxed) == 0) {
            enqueue(taskCategory, std::move(task));
        }
        else {
            auto& func = jobStorage.getFuncs(index);
            func.push_back(std::move(task));
        }

        return JobHandle::createHandle(id, std::move(setter));
    }

    template<typename Derived>
    JobHandle scheduleParalellJobHandle(std::shared_ptr<Derived>self, const size_t total, const size_t batchSize, const size_t workerCount)
    {
        stats_.onScheduled(1);

        auto jobId = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        const size_t numBatches = (total + batchSize - 1) / batchSize;

        const size_t threadSize = getThreadSize();
        const size_t workerNum = std::min({ threadSize,workerCount,numBatches });

        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));

        //self->taskCounter = numBatches;

        //self->nextBatch_.store(0, std::memory_order_relaxed);

        using ContextT = ParallelJobContext<Derived>;
        auto ctx = std::make_shared<ContextT>(
            self
            , jobId
            , total
            , batchSize
            , numBatches
            , chunkBatches
            , setter
            );

        auto index = getJobIndex(jobId);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = TaskCategory::Parallel;

        // ワーカー数分だけ Task を作成して登録
        for (size_t w = 0; w < workerNum; ++w) {
            auto work = [ctx](const size_t workerId) { executeIParallelJob(ctx->self.get(), ctx->jobId, ctx->setter.get(), ctx->batchSize, ctx->numBatches, ctx->total, ctx->chunkBatches,ctx->nextBatch,ctx->taskCounter,workerId); };

            Job job(std::move(work));

            enqueue(TaskCategory::Parallel, std::move(job));
        }

        return JobHandle::createHandle(jobId, std::move(setter));
    }

    //パラレルジョブの依存関係
    template<typename Derived>
    JobHandle scheduleParalellJobHandle(std::shared_ptr<Derived>self, const size_t total, const size_t batchSize, const size_t workerCount,JobHandle& depedentHandle)
    {
        stats_.onScheduled(1);

        auto jobId = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        const size_t numBatches = (total + batchSize - 1) / batchSize;

        const size_t threadSize = getThreadSize();
        const size_t workerNum = std::min({ threadSize,workerCount,numBatches });

        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));

        //self->taskCounter = numBatches;

        //self->nextBatch_.store(0, std::memory_order_relaxed);

        using ContextT = ParallelJobContext<Derived>;
        auto ctx = std::make_shared<ContextT>(
            self
            , jobId
            , total
            , batchSize
            , numBatches
            , chunkBatches
            , setter
            );

        auto index = getJobIndex(jobId);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = TaskCategory::Parallel;

        //依存解決用
        addDependent(jobId, depedentHandle);

        // ワーカー数分だけ Task を作成して登録
        if (jobInfo.inDegree.load(std::memory_order_relaxed) == 0) {
            
            for (size_t w = 0; w < workerNum; ++w) {
                auto work = [ctx](const size_t workerId) { executeIParallelJob(ctx->self.get(), ctx->jobId, ctx->setter.get(), ctx->batchSize, ctx->numBatches, ctx->total, ctx->chunkBatches, workerId); };

                Job job(std::move(work));

                enqueue(TaskCategory::Parallel, std::move(job));
            }
        }
        else {
            auto& funcs = jobStorage.getFuncs(index);

            for (size_t w = 0; w < workerNum; ++w) {
                auto work = [ctx](const size_t workerId) { executeIParallelJob(ctx->self.get(), ctx->jobId, ctx->setter.get(), ctx->batchSize, ctx->numBatches, ctx->total, ctx->chunkBatches, workerId); };

                Job job(std::move(work));
                
                funcs.push_back(std::move(job));
            }
        }

        return JobHandle::createHandle(jobId, std::move(setter));
    }

    template<typename Derived>
    JobHandle scheduleParalellJobHandle(std::shared_ptr<Derived>self, const size_t total, const size_t batchSize, const size_t workerCount, std::vector<JobHandle>& depedentHandles)
    {
        stats_.onScheduled(1);

        auto jobId = emplaceJobID();
        auto setter = std::make_shared<Inner>();

        const size_t numBatches = (total + batchSize - 1) / batchSize;

        const size_t threadSize = getThreadSize();
        const size_t workerNum = std::min({ threadSize,workerCount,numBatches });

        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));

        //self->taskCounter = numBatches;

        //self->nextBatch_.store(0, std::memory_order_relaxed);

        using ContextT = ParallelJobContext<Derived>;
        auto ctx = std::make_shared<ContextT>(
            self
            , jobId
            , total
            , batchSize
            , numBatches
            , chunkBatches
            , setter
            );

        auto index = getJobIndex(jobId);
        auto& jobInfo = jobStorage.getJobInfo(index);
        jobInfo.taskCategory = TaskCategory::Parallel;

        //依存解決用
        for (int i = 0; i < depedentHandles.size(); i++) {
            addDependent(jobId, depedentHandles[i]);
        }

        // ワーカー数分だけ Task を作成して登録
        if (jobInfo.inDegree.load(std::memory_order_relaxed) == 0) {

            for (size_t w = 0; w < workerNum; ++w) {
                auto work = [ctx](const size_t workerId) { executeIParallelJob(ctx->self.get(), ctx->jobId, ctx->setter.get(), ctx->batchSize, ctx->numBatches, ctx->total, ctx->chunkBatches, workerId); };

                Job job(std::move(work));

                enqueue(TaskCategory::Parallel, std::move(job));
            }
        }
        else {
            auto& funcs = jobStorage.getFuncs(index);

            for (size_t w = 0; w < workerNum; ++w) {
                auto work = [ctx](const size_t workerId) { executeIParallelJob(ctx->self.get(), ctx->jobId, ctx->setter.get(), ctx->batchSize, ctx->numBatches, ctx->total, ctx->chunkBatches, workerId); };

                Job job(std::move(work));
                funcs.push_back(std::move(job));
            }
        }

        return JobHandle::createHandle(jobId, std::move(setter));
    }

    //削除予定リストに追加、jobIdをNULLIDに変更
    void completedJob(const size_t workerId,JobId jobId);

    //ワーカーの数取得
    const size_t getThreadSize() const{ return threadSize;}

    bool stealChunk(ChunkMeta&chunk){
        return chunkQueue.try_dequeue(chunk);
    }

    bool isInitialized(){
        return initFlag;
    }

    //緊急停止フラグがたっているか
    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    JobEntry& getJobEntry(const JobId& jobId) {
        ASSERT(containsJob(jobId),"jobId is NULL or Deleted ID");

        return jobStorage.getJobInfo(getJobIndex(jobId));
    }

    bool containsJob(const JobId& jobId) const{
        return jobStorage.containsJob(jobId);
    }

    void processDependents(const size_t workerID,const JobId parent);

    bool getFlushChunk(ChunkMeta& chunk);

    //必ず最後のフレーム時に呼ぶようにする
    void cleanUpMainThreadOnLastFrame(){
        if(!initFlag||isAbort())return;

        jobStorage.cleanup(completedJobQueue);
    }

private:
    JobId emplaceJobID() {
        return jobStorage.emplaceJobId();
    }

    //タスクストレージにジョブを追加する
    void enqueue(TaskCategory taskCategory,Job&&job);

    //void enqueue(std::vector<Job>&& jobs);

    void abort();

    size_t getNextQueueIndex();

    //依存関係追加
    void addDependent(const JobId& child, JobHandle& parent);

    JobEntry& getJobInfo(const JobIndex index) {
        return jobStorage.getJobInfo(index);
    }

    std::vector<Job>& getFuncs(const JobIndex index){
        return jobStorage.getFuncs(index);
    }

    DependentNode& getDependents(const JobIndex index){
        return jobStorage.getDependents(index);
    }

    void scheduleDependentHandle(const size_t workerID,const JobIndex& childIndex);

    template<typename Derived>
    static void executeIJob(Derived* self, JobId jobId, Inner* setter, const size_t workerID) {
        self->execute();
        setter->setReady(true);

        auto& jm = JobManager::Instance();
        //依存解決
        jm.processDependents(workerID, jobId);

        //cleanUP用
        jm.completedJob(workerID, jobId);
    };

    template<typename Derived>
    static void executeIParallelJob(Derived* self, JobId jobId, Inner* setter, size_t batchSize, size_t numBatches, size_t total, size_t chunkBatches, std::atomic<size_t>& nextBatch, std::atomic<size_t>& taskCounter,size_t workerID) {

        while (true) {
            //回数見直し、バッチ回数に合わせる。応じて変える
            //numBatchesも
            const size_t idx = nextBatch.fetch_add(chunkBatches, std::memory_order_relaxed);

            if (idx >= numBatches) return;

            const size_t taken = std::min(chunkBatches, numBatches - idx);

            for (size_t b = 0; b < taken; ++b) {
                const size_t start = (idx + b) * batchSize;

                ASSERT(start < total, "IParallelJob workerEntry : start >= total");

                const size_t len = std::min(batchSize, total - start);

                self->ExecuteBatch(start, len, self);

                if (taskCounter.fetch_sub(taken, std::memory_order_relaxed) == taken) {
                    setter->setReady(true);

                    auto& jm = JobManager::Instance();

                    //依存解決
                    jm.processDependents(workerID, jobId);

                    //cleanUP用
                    jm.completedJob(workerID, jobId);
                    return;
                }
            }
        }
    }

private:
    JobStats stats_;

    size_t threadSize;

    //std::vector<std::unique_ptr<JobQueue>> localQueues;

    JobStorage jobStorage;

    //現ジョブの総数
    std::atomic<size_t> outstanding{ 0 };

    std::mutex stealMutex;

    std::mutex            finishMutex;
    std::condition_variable finishCv;

    std::atomic<size_t>      nextQueue{ 0 };

    std::unique_ptr<TimelineRecorder> recorder;

    JobBarrier barrier;

    //初期化、終了、停止フラグ
    bool initFlag;
    std::atomic<bool> stopFlag;
    std::atomic<bool> abortFlag{ false };

    std::vector<std::unique_ptr<IWorker>> workers;

    moodycamel::ConcurrentQueue<ChunkMeta>chunkQueue;
    moodycamel::ConcurrentQueue<JobId>completedJobQueue;

    moodycamel::ProducerToken chunkToken;
    moodycamel::ProducerToken completedJobToken;

    std::mutex batchMutex;
    ChunkMeta currentBatchChunk;

    static constexpr size_t batchmaxchunksize = 34;
    //static constexpr size_t batchmaxchunksize = 2048;
};

} //namespace ECS::JobSystem