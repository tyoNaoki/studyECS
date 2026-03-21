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
#include "CallbackList.hpp"
#include "JobWorker.h"

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
        JobCategory jobCategory{};
        TaskCategory taskCategory{};

        JobEntry() = default;

        // ムーブコンストラクタ
        JobEntry(JobEntry&& other) noexcept
            : inDegree(other.inDegree.load())
            , jobCategory(other.jobCategory)
            , taskCategory(other.taskCategory)
        {
        }

        // ムーブ代入
        JobEntry& operator=(JobEntry&& other) noexcept {
            if (this != &other) {
                inDegree.store(other.inDegree.load());
                jobCategory = other.jobCategory;
                taskCategory = other.taskCategory;
            }

            return *this;
        }
    };

    struct JobStorage {
        void addDependent(size_t child,size_t parent){
            auto& childJob = getJobInfo(child);

            //std::lock_guard<std::mutex> lk(dependentLocks[sparse[getJobIndex(parent)]]);

            if (getFunc(parent).valid()) { // まだ実行されていない
                //childを親のnextDependentに差し込む
                getDependents(parent).push_back(child);

                //子ジョブの未解決依存数を増やす
                childJob.inDegree.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void reserveJobs(size_t reserveCount) {
            jobIds.reserve(reserveCount);
        }

        void clearAll() {
            std::lock_guard<std::mutex> lk(lock);

            waitQueue.clear();
            dependents.clear();
            //dependentLocks.clear();
            jobIds.clear();
            freeIds.clear();
            removeJobData.clear();
            nextIndex = 0;
        }

        //JobIDを返す
        JobId emplaceJobId() {
            std::lock_guard<std::mutex> lk(lock);

            JobId newId = allocateJobId();

            auto jobIndex = getJobIndex(newId);

            if(jobIds.size() <= jobIndex){
                emplaceTask();
            }

            ASSERT(jobIds.size()-1 <= jobIndex,"over jobIds %zu", getJobIndex(newId));
            jobIds[jobIndex] = newId;
            
            return newId;
        }

        void emplaceTask(){
            jobInfos.emplace_back();
            jobs.emplace_back();
            dependents.emplace_back();
            inners.emplace_back(nullptr);
            jobIds.emplace_back();
        }

        void pushBackWaitQueue(size_t jobIndex) {
            std::lock_guard<std::mutex> lk(lock);

            waitQueue.push_back(jobIndex);
        }

        JobId& getWaitJob(const size_t jobIndex) {
            return waitQueue[jobIndex];
        }

        JobEntry& getJobInfo(size_t index){
            return jobInfos[index];
        }

        Job& getFunc(size_t index){
            return jobs[index];
        }

        std::vector<JobId>& getDependents(size_t index) {
            return dependents[index];
        }

        std::shared_ptr<Inner>& getInner(size_t index) {
            return inners[index];
        }

        //schedule時に対応ジョブに関数ポインターを割り当てる
        /*template<class DerivedJob>
        void createJobFunction(const JobId& id){
            jobData[getJobIndex(id)].func = &Invoke<DerivedJob>;
        }*/

        void removeJob(JobId jobId){
            std::lock_guard<std::mutex> lk(lock);

            jobIds[getJobIndex(jobId)] = NULL_JOB_ID;
            freeIds.push_back(jobId);
        }

        void removeJobs(std::vector<JobId> jobs) {
            std::lock_guard<std::mutex> lk(lock);

            for(size_t i = 0;i<jobs.size();i++){
                jobIds[getJobIndex(jobs[i])] = NULL_JOB_ID;
                freeIds.push_back(jobs[i]);
            }
        }

        bool containsJob(const JobId id) const{
            JobIndex index = getJobIndex(id);
            return index < jobIds.size() && jobIds[index] == id;
        }

        //template<class T>
        //static void Invoke(IJobBase* raw) {
        //    //static_cast<T*>(raw)->Execute();
        //    //raw->Execute();
        //}

        JobId allocateJobId() {
            if(!freeIds.empty()){
                // removeの時以外でfreeIdsが使用されないので、ロックレスで問題なし
                JobId old = freeIds.back();
                freeIds.pop_back();

                return composeJobId(getJobIndex(old), getJobVersion(old) + 1);
            }
            
            return composeJobId(nextIndex++,0u);
        }

    private:
        void removeWaitJob(size_t removeJobIndex){
            /*auto& waitJob = waitJobs.back();
            auto rastJobIndex = getJobIndex(waitJob.jobId);
            auto swapId = waitJobs[removeJobIndex].jobId;
            
            sparse[rastJobIndex] = removeJobIndex;
            sparse[swapId] = NULL_JOB_INDEX;

            std::swap(waitJobs[removeJobIndex],waitJobs.back());
            waitJobs.pop_back();*/
        }

        void swap(size_t job,size_t job2){
            std::swap(jobInfos[job],jobInfos[job2]);
            std::swap(jobs[job], jobs[job2]);
            std::swap(dependents[job], dependents[job2]);
            std::swap(inners[job], inners[job2]);
        }

        //void removeJob(JobId& id) {
        //    JobId removeId = jobIds[getJobIndex(id)];
        //    JobIndex removeIndex = getJobIndex(removeId);

        //    std::lock_guard<std::mutex> lk(lock);

        //    //すでに無効
        //    ASSERT(jobData.empty() || removeIndex >= jobData.size(),"this id is NULL");

        //    jobIds.back() = removeId;

        //    std::swap(jobData[removeIndex], jobData.back());
        //    std::swap(jobIds[removeIndex], jobIds.back());

        //    jobData.pop_back();
        //    jobIds.pop_back();
        //    //dependentLocks.pop_back();

        //    sparse[id] = NULL_JOB_INDEX;
        //    freeIds.push_back(id);
        //    id = NULL_JOB_ID;
        //}

    private:
        //関数ポインター,IJobBase*dataが入っている
        std::vector<size_t>waitQueue;

        std::vector<size_t>readyQueue;

        std::vector<JobEntry>jobInfos;
        std::vector<Job>jobs;
        std::vector<std::vector<size_t>>dependents;

        std::vector<std::shared_ptr<Inner>>inners;
        //std::vector<std::mutex> dependentLocks;

        //JobIdのリスト
        std::vector<JobId>jobIds;

        std::vector<JobId>removeJobData;

        JobIndex nextIndex{ 0 };
        std::vector<JobId> freeIds;
        std::mutex lock;
    };

    static constexpr size_t NumCategories = static_cast<size_t>(JobCategory::Num);

    /// <summary>
    /*pending	キューに積まれ、まだ取得・実行が始まっていないジョブ数	pushBottom() 後、popOrSteal() 前
      running	ワーカーが取得し、現在実行中のジョブ数   popOrSteal() 成功直後 ～ 完了まで
      completed	実行が終わり、後片付けや結果格納も含めて完了したジョブ数	runChunk / runJob() 実行後*/
    /// </summary>
    class JobStats {
        friend class JobManager;

        // pending の増減
        //void onEnqueued(const JobCategory cat, size_t count) noexcept {
        //    pending_[size_t(cat)].fetch_add(count, std::memory_order_release);
        //}

        //void onDequeued(const JobCategory cat, size_t count) noexcept {
        //    size_t c = pending_[size_t(cat)].fetch_sub(count, std::memory_order_release);

        //    if (c < count) {
        //        std::printf("pending count %zu, sub count is %zu\n", c, count);
        //    }
        //}

        //// running の増減
        //void onStart(const JobCategory cat, size_t count) noexcept {
        //    running_[size_t(cat)].fetch_add(count, std::memory_order_release);
        //}

        void onJobFinish(const JobCategory cat, size_t count) noexcept;

    public:
        void onScheduled(const JobCategory cat, size_t count) noexcept {
            scheduled[size_t(cat)].fetch_add(count, std::memory_order_release);
        }

        // フレーム終端で指定カテゴリがすべて完了するまで待つ
        void waitForAll(const JobCategory cat);

        size_t scheduledJobCount(const JobCategory cat) const noexcept{
            ASSERT(size_t(cat) < size_t(JobCategory::Num),"JobCategroy is falid num");

            return scheduled[size_t(cat)].load(std::memory_order_acquire);
        }

       /* size_t pendingJobCount(const JobCategory cat) const noexcept{
            return pending_[size_t(cat)].load(std::memory_order_acquire);
        }

        size_t runningJobCount(const JobCategory cat) const noexcept {
            return running_[size_t(cat)].load(std::memory_order_acquire);
        }

        size_t completedJobCount(const JobCategory cat) const noexcept {
            return completed_[size_t(cat)].load(std::memory_order_acquire);
        }

        size_t notCompletedJobCount(const JobCategory cat)const noexcept{
            return pending_[size_t(cat)].load(std::memory_order_acquire) + running_[size_t(cat)].load(std::memory_order_acquire);
        }*/

    private:
        // 各状態のカテゴリ別カウンタ
        std::array<std::atomic<size_t>, NumCategories> scheduled{};
        /*std::array<std::atomic<size_t>, NumCategories> pending_{};
        std::array<std::atomic<size_t>, NumCategories> running_{};
        std::array<std::atomic<size_t>, NumCategories> completed_{};*/

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

    using RealTimeOnlyWorker = Worker<JobQueue,RealTimePolicy>;

    static constexpr size_t NULL_RESULT = std::numeric_limits<size_t>::max();

    //仮として60FPS
    static constexpr float targetFPS = 60.0f;

    //1フレームあたりの時間
    static constexpr float frameTimeMs = 1000.0f / targetFPS;

    static constexpr double safetyMarginMs = 1.5;

    static constexpr double bgRatioMin = 0.10;

    static constexpr double bgRatioMax = 0.50;
    //TaskQueueパラメータ設定
    static constexpr size_t realTimeCap = 100'000;
    static constexpr size_t backGroundCap = 1'000;

    //512
    static constexpr size_t realTimeChunkSize = 64;
    static constexpr size_t backGroundChunkSize = 512;

    static constexpr size_t slotWorkCapacity = 32;

    JobManager() = default;

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    struct Executor {
        void runJob(size_t workerId, JobId* Id);

        void runSlot(size_t workerId, JobId* begin, JobId* end);

        void runChunk(size_t workerId, ChunkMeta&& chunk);
    private:
        void processDependents(JobEntry& parent);

    };

public:
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

    void Initialize(size_t reserveJobNum,size_t threadCount,
        std::unique_ptr<TimelineRecorder> rec = nullptr);

    ~JobManager();

    void start(){
        barrier.start();
    }

    template<typename... Args>
    void log(const Args&... args) {
        (localLogBuffer << ... << args) << "\n";
    }

    // フレーム終端や waitForAll で呼ぶ
    void flushLogs() {
        std::lock_guard<std::mutex> lk(logMutex_);
        std::cout << localLogBuffer.str();
        localLogBuffer.str("");
        localLogBuffer.clear();
    }

    bool containsJob(const JobId jobId) const{
        return jobStorage.containsJob(jobId);
    }

    //ここに
    /*template<
        typename T,
        typename... Args
    >
    TaskFuture<T> createJob(TaskCategory TC = TaskCategory::Easy, JobCategory JC = JobCategory::RealTime,Args&&... args){
        JobId id = jobStorage.emplaceJobData<T>(std::forward<Args>(args)...);
        jobStorage.createJobFunction<T>(id);

        auto&entry = jobStorage.getJobEntry(id);
        entry.jobCategory = JC;
        entry.taskCategory = TC;

        return TaskFuture<T>(id);
    }*/

    //template<
    //    typename T,
    //    typename... Args
    //>
    //    T createIJob(TaskCategory TC = TaskCategory::Easy, JobCategory JC = JobCategory::RealTime, Args&&... args) {
    // JobId id = jobStorage.emplaceJobID();
    //    //jobStorage.createJobFunction<T>(id);
    // auto& entry = jm.getJobEntry(id);
    //entry.jobCategory = JC;
    //entry.taskCategory = TC;

    //    //T job(std::forward<Args>(args)...); 

    //    return job;
    //}

    //job = JobHandle.dependents(handle1,handle2);

    JobHandle scheduleJobHandle(TaskCategory taskCategory,JobCategory jobCategory,Job&&job);

    //JobHandle scheduleJobHandle(JobId jobId, JobHandle& handle);

    //JobHandle scheduleJobHandle(JobId jobId,std::vector<JobHandle>&jobHandles);

    //JobHandle scheduleJobHandle(JobId jobId,Job&&job, std::vector<JobHandle>& jobHandles);

    void scheduleDependentHandles(std::vector<JobId>&&jobs) {

#ifdef DEBUG
        //jobsのスケジュール済みかどうかチェック
        ASSERT(!jobs.empty(),"jobs is empty");

        for(size_t i = 0;i<jobs.size();i++){
            auto& entry = getJobEntry(jobs[i].jobId);
            ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");
        }

#endif // DEBUG

        auto&entry = getJobEntry(jobs[0]);

        taskStorage.enqueue(entry.taskCategory, std::move(jobs));

        popChunks();
    }

    void scheduleDependentHandle(JobId childId){
        auto& entry = getJobEntry(childId);
        //ASSERT(!entry.inner->isReady(), "this job is executed");

        enqueue(entry.taskCategory, entry.jobCategory, childId);
    }

   /// <summary>
   /// Jobにコマンド形式で関数ポインターを渡していく
   /// job.AddRequest(jobHandle,([&capture](JobClass& t) {
   ///     t.temp = capture;
   /// }));
   /// </summary>
   /// <returns></returns>
    template<typename JobT>
    void addCommand(const JobId&jobId, std::function<void(JobT&)>&&cmd){
        JobT* job = static_cast<JobT>(getJobEntry(jobId).data);
        job->AddRequeset(std::move(cmd));
    }

    //すべてのワーカーが順調に稼働しているかチェックする
    bool checkRanAllJobInJobQueues();

    //削除予定リストに追加、jobIdをNULLIDに変更
    void removeJob(JobId& jobId);

    //バックグラウンドで動かすJobを詰めたストレージからchunkをpopする
    void popGlobalBackGroundQueue();

    //ワーカーの数取得
    const size_t getThreadSize() const{ return threadSize;}

public:
    //すべてのchunk化されていないjobも含めて、すべてのワーカーのローカルキューに順番に詰めていく。
    void allFlushJob(const JobCategory category);

    //chunkをワーカーから盗む（メインスレッド専用)
    bool stealChunk(const JobCategory category,ChunkMeta&chunk);

    //chunkとして完成していないchunkも含めて、一つchunk化されたJob群を取得する
    void getFlushChunk(const JobCategory category,ChunkMeta&chunk);

    //緊急停止フラグがたっているか
    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    JobEntry& getJobEntry(const JobId& jobId) {
        return jobStorage.getJobInfo(getJobIndex(jobId));
    }

private:
    //タスクストレージにジョブを追加する
    void enqueue(TaskCategory taskCategory,JobCategory jobCategory,JobId jobId);

    void popChunks();

    void popChunk(ChunkMeta&chunk);

    bool allQueuesEmpty() const;

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

    //依存関係追加
    void addDependent(const JobId& child,const JobId& parent);

    //依存関係追加
    void addDependent(const JobId& child, JobHandle& parent);

    JobEntry& getDense(const size_t dense) {
        return jobStorage.getJobInfo(dense);
    }

    Job& getJob(const size_t dense){
        return jobStorage.getFunc(dense);
    }

    Inner* getInner(const size_t dense){
        return jobStorage.getInner(dense).get();
    }

    bool clearTaskStorage(JobCategory category);

private:
    double avg_JobTimeMs = 1.0f;
    double avg_ExecuteJobTime = 0.1;

    JobStats stats_;

    size_t threadSize;
    
    //ログ出力
    std::mutex logMutex_;
    inline static thread_local std::ostringstream localLogBuffer;

    std::vector<std::unique_ptr<JobQueue>> localQueues;

    TaskArena taskStorage;

    JobStorage jobStorage;

    //現ジョブの総数
    std::atomic<size_t> outstanding{ 0 };

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::mutex            finishMutex;
    std::condition_variable finishCv;

    std::atomic<size_t>      nextQueue{ 0 };
    std::condition_variable condition;

    std::unique_ptr<TimelineRecorder> recorder;

    JobBarrier barrier;

    //初期化、終了、停止フラグ
    bool initFlag;
    std::atomic<bool> stopFlag;
    std::atomic<bool> abortFlag{ false };

    std::vector<std::unique_ptr<IWorker>> workers;
};

struct IFuture;

struct CombineJobHandles {
    bool isComplete() const;

    void Complete() const;

    void AddJob(IFuture& future) const{};

private:
    std::vector<JobId> jobIds;
    std::shared_ptr<Inner>inner;

    // デフォルトコンストラクタ
    CombineJobHandles() = default;

    friend class JobManager;

    // 外部には見せない
    static CombineJobHandles createHandle(IFuture&future, std::shared_ptr<Inner> inner) {
        CombineJobHandles h;
        //h.jobIds.push = std::move(ids);
        //h.inner = std::move(inner);
        return h;
    }
};

struct IFuture {

protected:
    JobId Id;
};

template<typename Derived_t>
struct TaskFuture : public IFuture{
    
    using type = Derived_t;

    explicit TaskFuture(JobId id){ Id = id;}

    ~TaskFuture(){
        auto&jm = JobManager::Instance();

        jm.removeJob(Id);
    }

    template<typename F>
    void addCommands(F&& f) {
        commands.emplace_back(std::forward<F>(f));
    }

    ////その場で実行。
    void ExecuteJob(){
     
        auto& jm = JobManager::Instance();
        ASSERT(jm.containsJob(Id), "job not contains");
        auto& job = jm.getJobEntry(Id);

        ASSERT(false,"not work");
        jm.getJob(Id).invoke();
        jm.getInner()->setReady(true);
    }

    //JobHandle schedule() {
    //    auto& jm = JobManager::Instance();
    //    auto& job = jm.getJobEntry(Id);

    //    //auto* derived = static_cast<Derived_t*>(job.data);
    //    //AddRequest(derived);

    //    return jm.scheduleJobHandle(Id);
    //}

    /*JobHandle schedule(JobHandle& handle) {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        auto* derived = static_cast<Derived_t*>(job.data);
        AddRequest(derived);

        return jm.scheduleJobHandle(Id, handle);
    }

    JobHandle schedule(std::vector<JobHandle>&dependents){
        auto&jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        auto* derived = static_cast<Derived_t*>(job.data);
        AddRequest(derived);

        return jm.scheduleJobHandle(Id, dependents);
    }*/

    //type* getJob() const {
    //    auto& jm = JobManager::Instance();
    //    auto& job = jm.getJobEntry(Id);

    //    //Jobを返す
    //    return static_cast<type*>(job.data);
    //}

private:
    void AddRequest(Derived_t* derived){
        if(commands.empty()) return;

        derived->AddRequest(std::move(commands));
        commands.clear();
    }

private:
    std::vector<std::function<void(Derived_t&)>>commands;
};

} //namespace ECS::JobSystem