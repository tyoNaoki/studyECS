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

    struct Inner {
        std::atomic<bool>ready = false;

        Inner& operator=(Inner&& other) noexcept {
            ready.store(other.ready.load(std::memory_order_relaxed));

            return *this;
        }

        void swap(std::shared_ptr<Inner>&& inner) {
            auto r = inner->ready.load(std::memory_order_relaxed);
            auto r2 = ready.load(std::memory_order_relaxed);

            ready.store(r, std::memory_order_relaxed);
            inner->ready.store(r2, std::memory_order_relaxed);
        }
    };

    

    enum class JobStatus : uint8_t{
            UnSchedule,   // スケジュール前
            Scheduled,   // 実行待ち
            Running,     // 実行中
            Completed    //実行完了
    };

    struct JobEntry {
        using Invoker = void(*)(IJobBase*);

        Invoker func = nullptr;

        //null時:未作成
        IJobBase* data = nullptr;

        JobCategory jobCategory;
        TaskCategory taskCategory;

        std::shared_ptr<Inner>inner;

        JobEntry() = default;
        JobEntry(Invoker f, IJobBase* d) : func(f), data(d) {}

        // ムーブ可能にする
        JobEntry(JobEntry&& other) noexcept {
            func = other.func;
            data = other.data;
            jobCategory = other.jobCategory;
            taskCategory = other.taskCategory;
            inner = std::move(other.inner);
        }

        JobEntry& operator=(JobEntry&& other) noexcept {
            if (this != &other) {
                func = other.func;
                data = other.data;
                jobCategory = other.jobCategory;
                taskCategory = other.taskCategory;
                inner = other.inner;
            }

            return *this;
        }

        void swap(JobEntry& other) noexcept {
            using std::swap;
            swap(func, other.func);
            swap(data, other.data);
            swap(jobCategory, other.jobCategory);
            swap(taskCategory, other.taskCategory);
            other.inner->swap(std::move(other.inner));
        }

    };

    struct JobStorage {
        void reserveJobs(size_t reserveCount) {
            sparse.reserve(reserveCount);
            jobData.reserve(reserveCount);
            jobIds.reserve(reserveCount);
        }

        void clearAll() {
            std::lock_guard<std::mutex> lk(lock);

            sparse.clear();
            jobData.clear();
            jobIds.clear();
            freeIds.clear();
            removeJobData.clear();
            nextIndex = 0;
        }

        //JobIDを返す。
        template<class DerivedJob, class... Args>
        JobId emplaceJobData(Args&&... args) {
            std::lock_guard<std::mutex> lk(lock);

            JobId newId = allocateJobId();

            if(sparse.size() <= newId){
                sparse.emplace_back();
                sparse.back() = NULL_JOB_ID;
            }

            ASSERT(sparse[getJobIndex(newId)] == NULL_JOB_ID,"valid sparse slot do not use");

            auto* p = new DerivedJob(std::forward<Args>(args)...);
            const size_t newIndex = jobData.size();
            jobData.emplace_back(nullptr,std::move(p));
            jobIds.push_back(newId);

            sparse[getJobIndex(newId)] = newIndex;

            return newId;
        }

        //schedule時に対応ジョブに関数ポインターを割り当てる
        template<class DerivedJob>
        void createJobFunction(const JobId& id){
            jobData[getJobIndex(id)].func = &Invoke<DerivedJob>;
        }

        void addRemoveJob(JobId jodId){removeJobData.push_back(jodId);}

        void addRemoveJobs(std::vector<JobId>&& jobs) {
            removeJobData.insert(removeJobData.end(),
                std::move_iterator(jobs.begin()),
                std::move_iterator(jobs.end()));
        }

        //GetJobEntry関数の参照が壊れるので、絶対にFrameの最後全てのジョブを処理か、処理をしていないタイミングで行うこと!!
        void removeJobs() {
            std::lock_guard<std::mutex> lk(lock);

            std::sort(removeJobData.begin(), removeJobData.end(),
                [&](JobId& a, JobId& b) {
                    return sparse[getJobIndex(a)] > sparse[getJobIndex(b)]; // removeIndex の大きい順
                });

            for (auto& removeId : removeJobData) {
                removeJob(removeId);
            }

            removeJobData.clear();
        }

        JobEntry& getJobEntry(const JobId id){
            return jobData[sparse[getJobIndex(id)]];
        }

        JobEntry& getDense(const size_t denseIndex) {
            return jobData[denseIndex];
        }

        size_t getDenseIndex(const JobId id){
            return sparse[getJobIndex(id)];
        }

        bool containsJob(const JobId id) const{
            JobIndex index = getJobIndex(id);
            return index < sparse.size() && sparse[index] < jobIds.size() && jobIds[sparse[index]] == id;
        }

        template<class T>
        static void Invoke(IJobBase* raw) {
            static_cast<T*>(raw)->Execute();
        }

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
        void removeJob(JobId& id) {
            JobId removeId = jobIds[getJobIndex(id)];
            JobIndex removeIndex = getJobIndex(removeId);

            std::lock_guard<std::mutex> lk(lock);

            //すでに無効
            ASSERT(jobData.empty() || removeIndex >= jobData.size(),"this id is NULL");

            jobIds.back() = removeId;

            std::swap(jobData[removeIndex], jobData.back());
            std::swap(jobIds[removeIndex], jobIds.back());

            jobData.pop_back();
            jobIds.pop_back();

            sparse[id] = NULL_JOB_INDEX;
            freeIds.push_back(id);
            id = NULL_JOB_ID;
        }

    private:
        //内部に入っているのは、JobDataのIndex
        std::vector<size_t>sparse;

        //関数ポインター,IJobBase*dataが入っている
        std::vector<JobEntry>jobData;
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
        void processDependents(IJobBase* parent);
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
    template<
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
    }

    //job = JobHandle.dependents(handle1,handle2);

    template<typename T>
    JobHandle scheduleJobHandle(TaskFuture<T>&future) {

        auto&entry = getJobEntry(future.Id);

        ASSERT(!entry.inner||entry.inner->ready, "this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする
        stats_.onScheduled(entry.jobCategory,1);
        entry.inner = std::make_shared<Inner>();
        JobHandle jobHandle{ future.Id,entry.inner };

        if(entry.data->inDegree.load(std::memory_order_acquire) == 0){
            enqueue(entry.taskCategory, entry.jobCategory, future.Id);
        }

        return jobHandle;
    }

    template<typename T>
    JobHandle scheduleJobHandle(TaskFuture<T>& future, JobHandle& handle) {

        auto& entry = getJobEntry(future.Id);

        ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする
        stats_.onScheduled(entry.jobCategory, 1);
        entry.inner = std::make_shared<Inner>();
        JobHandle jobHandle{ future.Id,entry.inner };

        addDependent(future.Id, handle);

        if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
            enqueue(entry.taskCategory, entry.jobCategory, future.Id);
        }

        return jobHandle;
    }

    template<typename T>
    JobHandle scheduleJobHandle(TaskFuture<T>& future,std::vector<JobHandle>&&jobHandles) {

        auto& entry = getJobEntry(future.Id);

        ASSERT(!entry.inner || entry.inner->ready, "this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする
        stats_.onScheduled(entry.jobCategory, 1);
        entry.inner = std::make_shared<Inner>();
        JobHandle jobHandle{ future.Id,entry.inner};

        for(auto&parent : jobHandles){
            addDependent(future.Id,parent.jobId);
        }

        if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
            enqueue(entry.taskCategory, entry.jobCategory, jobHandle);
        }

        return jobHandle;
    }

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
        ASSERT(!entry.inner->ready, "this job is executed");

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

    bool checkRanAllJobInJobQueues();

    void addRemoveJob(JobId& jobId);

    void removeJobsOnLastFrame();

    void popGlobalBackGroundQueue();

    const size_t getThreadSize() const{ return threadSize;}

public:
    void allFlushJob(const JobCategory category);

    bool stealChunk(const JobCategory category,ChunkMeta&chunk);

    void getFlushChunk(const JobCategory category,ChunkMeta&chunk);

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    JobEntry& getJobEntry(const JobId jobId) {
        return jobStorage.getJobEntry(jobId);
    }

private:
    void enqueue(TaskCategory taskCategory,JobCategory jobCategory,JobId jobId);

    void popChunks();

    void popChunk(ChunkMeta&chunk);

    bool allQueuesEmpty() const;

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

    //依存関係追加
    void addDependent(const JobId& child,const JobId& parent);

    void addDependent(const JobId& child, JobHandle& parent);

    JobEntry& getDense(const size_t dense) {
        return jobStorage.getDense(dense);
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

struct JobHandle {
    JobId jobId;
    std::shared_ptr<Inner>inner;

    // デフォルトコンストラクタ
    JobHandle() = delete;

    bool isComplete() const {

        ////実行可否
        return inner->ready;
    }

    void Complete() const {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(jobId);

        ////実行完了するまでChunkをフラッシュしてChunk実行し続ける
        while (!inner->ready) {

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
};

template<typename Derived_t>
struct TaskFuture {
    JobId Id;

    using type = Derived_t;
    using Return_t = typename type::Return_t;

    explicit TaskFuture(JobId id) : Id(id) {}

    ~TaskFuture(){
        auto&jm = JobManager::Instance();

        jm.addRemoveJob(Id);
    }

    ////その場で実行。
    void ExecuteJob(){
     
        auto& jm = JobManager::Instance();
        ASSERT(jm.containsJob(Id), "job not contains");
        auto& job = jm.getJobEntry(Id);

        job.func(job.data);
        job.inner->ready = true;
    }

    JobHandle schedule() {
        auto& jm = JobManager::Instance();
        return jm.scheduleJobHandle(*this);
    }

    JobHandle schedule(std::vector<std::function<void(Derived_t&)>>&& commands) {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        auto* derived = static_cast<Derived_t*>(job.data);
        derived->AddRequest(std::move(commands));

        return jm.scheduleJobHandle(*this);
    }

    JobHandle schedule(JobHandle&handle) {
        auto& jm = JobManager::Instance();
        return jm.scheduleJobHandle(*this,handle);
    }

    JobHandle schedule(std::vector<std::function<void(Derived_t&)>>&& commands,JobHandle& handle) {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        auto* derived = static_cast<Derived_t*>(job.data);
        derived->AddRequest(std::move(commands));

        return jm.scheduleJobHandle(*this, handle);
    }

    JobHandle schedule(std::vector<JobHandle>&& dependents) {
        auto& jm = JobManager::Instance();
        return jm.scheduleJobHandle(*this, std::move(dependents));
    }

    JobHandle schedule(std::vector<std::function<void(Derived_t&)>>&&commands,std::vector<JobHandle>&&dependents){
        auto&jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        auto* derived = static_cast<Derived_t*>(job.data);
        derived->AddRequest(std::move(commands));

        return jm.scheduleJobHandle(*this, std::move(dependents));
    }

    type* getJob() const {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        //Jobを返す
        return static_cast<type*>(job.data);
    }
};

} //namespace ECS::JobSystem