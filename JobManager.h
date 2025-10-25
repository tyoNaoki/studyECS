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

        std::atomic<JobStatus> status{JobStatus::UnSchedule};

        JobEntry() = default;
        JobEntry(Invoker f, IJobBase* d) : func(f), data(d),status(JobStatus::UnSchedule) {}

        // ムーブ可能にする
        JobEntry(JobEntry&& other) noexcept {
            func = other.func;
            data = other.data;
            status.store(other.status.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
        }

        JobEntry& operator=(JobEntry&& other) noexcept {
            if (this != &other) {
                func = other.func;
                data = other.data;
                status.store(other.status.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
            }
            return *this;
        }

        void swap(JobEntry& other) noexcept {
            std::swap(func, other.func);
            std::swap(data, other.data);
            // atomic は直接 swap できないので load/store 経由
            auto s1 = status.load(std::memory_order_relaxed);
            auto s2 = other.status.load(std::memory_order_relaxed);
            status.store(s2, std::memory_order_relaxed);
            other.status.store(s1, std::memory_order_relaxed);
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

            if(sparse.size()<=newId){
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

        void addRemoveJob(JobHandle handle){removeJobData.push_back(handle);}

        void addRemoveJobs(std::vector<JobHandle>&& jobs) {
            removeJobData.insert(removeJobData.end(),
                std::move_iterator(jobs.begin()),
                std::move_iterator(jobs.end()));
        }

        //GetJobEntry関数の参照が壊れるので、絶対にFrameの最後全てのジョブを処理か、処理をしていないタイミングで行うこと!!
        void removeJobs() {
            std::lock_guard<std::mutex> lk(lock);

            std::sort(removeJobData.begin(), removeJobData.end(),
                [&](JobHandle& a, JobHandle& b) {
                    return sparse[getJobIndex(a.jobId)] > sparse[getJobIndex(b.jobId)]; // removeIndex の大きい順
                });

            for (auto& removeHandle : removeJobData) {
                removeJob(removeHandle.jobId);
            }

            removeJobData.clear();
        }

        JobEntry& getJobEntry(const JobId id){
            return jobData[sparse[getJobIndex(id)]];
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

        std::vector<JobHandle>removeJobData;

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

        void onScheduled(const JobCategory cat, size_t count) noexcept{
            scheduled[size_t(cat)].fetch_add(count, std::memory_order_release);
        }

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

class JobManager
{
    /*template<typename T>
    class intrusive_ptr;*/

    using TaskPtr = intrusive_ptr<Task>;

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
    static constexpr size_t realTimeCap = 180'000;
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
        void runJob(size_t workerId, JobHandle* handle);

        void runSlot(size_t workerId, JobHandle* begin, JobHandle* end);

        void runChunk(size_t workerId, ChunkMeta* chunk);

    private:
        void processDependents(IJobBase* parentJob);
    };

public:
    static JobManager& Instance() {
        static JobManager manager;
        return manager;
    }

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
        //すでにスケジュール済み
        ASSERT(entry.status != JobStatus::Scheduled,"this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする
        stats_.onScheduled(entry.jobCategory,1);
        JobHandle jobHandle{ future.Id };

        if(entry.data->inDegree.load(std::memory_order_acquire) == 0){
            enqueue(entry.taskCategory,jobHandle);
        }

        entry.status = JobStatus::Scheduled;

        return jobHandle;
    }

    JobHandle scheduleJobHandle(JobId jobId) {

        auto& entry = getJobEntry(jobId);
        //すでにスケジュール済み
        ASSERT(entry.status != JobStatus::Scheduled, "this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする

        stats_.onScheduled(entry.jobCategory, 1);
        JobHandle handle{jobId };

        if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
            enqueue(entry.taskCategory,std::move(handle));
        }

        entry.status = JobStatus::Scheduled;

        return handle;
    }

    std::vector<JobHandle>&& scheduleJobHandles(std::vector<JobId>&&jobs) {

        ////すでにスケジュール済み
        //ASSERT(entry.status != JobStatus::Scheduled, "this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする
        stats_.onScheduled(getJobEntry(jobs[0]).jobCategory, jobs.size());
        std::vector<JobHandle>handles;
        handles.reserve(jobs.size());

        for(int i = 0;i< handles.size();i++){
            auto&entry = getJobEntry(jobs[i]);
            handles[i].jobId = jobs[i];
            entry.status = JobStatus::Scheduled;
        }

        /*if (entry.data->inDegree.load(std::memory_order_acquire) == 0) {
            enqueue(jobHandle);
        }*/

        return std::move(handles);
    }

    void scheduleDependentHandle(JobHandle childHandle){
        //auto& entry = getJobEntry(childHandle.jobId);
        ////まだスケジュールしていない
        //ASSERT(entry.status == JobStatus::Scheduled, "this job is not schedule");
        //enqueue(childHandle);
    }

    //依存関係追加
    template<typename T>
    void addDependent(JobId& childId, JobId& parentId) {
        auto& jm = JobManager::Instance();

        ASSERT(childId != parentId,"addDependent() do not use childJob and childJob");

        auto& child =jm.getJobEntry(childId);
        auto& parent = jm.getJobEntry(parentId);

        std::lock_guard<std::mutex> lk(parent.data->dependentLock);
        if (parent.func) { // まだ実行されていない
            // childを親のnextDependentに差し込む
            child.data->nextDependent = parent.data->nextDependent;
            parent.data->nextDependent = childId;

            // 子ジョブの未解決依存数を増やす
            child->inDegree.fetch_add(1);
        }
    }

   /// <summary>
   /// Jobにコマンド形式で関数ポインターを渡していく
   /// job.AddRequest(jobHandle,([&capture](JobClass& t) {
   ///     t.temp = capture;
   /// }));
   /// </summary>
   /// <returns></returns>
    template<typename JobT>
    void addCommand(const JobHandle&handle, std::function<void(JobT&)>&&cmd){
        JobT* job = static_cast<JobT>(getJobEntry(handle.jobId).data);
        job->AddRequeset(std::move(cmd));
    }

    bool checkRanAllJobInJobQueues();

    void addRemoveJob(JobHandle& handle);

    void removeJobsOnLastFrame();

    void popGlobalBackGroundQueue();

    const size_t getThreadSize() const{ return threadSize;}
public:
    void allFlushJob(const JobCategory category);

    void getFlushChunk(const JobCategory category,ChunkMeta*&chunk);

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

private:
    void enqueue(TaskCategory taskCategory,JobHandle handle);

    void popChunk();

    void popChunk(ChunkMeta*&chunk);

    bool allQueuesEmpty() const;

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

    JobEntry& getJobEntry(const JobId jobId) {
        return jobStorage.getJobEntry(jobId);
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

template<typename Derived_t>
struct TaskFuture {
    const JobId Id;

    using type = Derived_t;
    using Return_t = typename type::Return_t;

    explicit TaskFuture(JobId id) : Id(id) {}

    //その場で実行。
    void ExecuteJob(){
        while(true){
            return;
        }
    }

    template<typename T = Return_t,
        typename = std::enable_if_t<std::is_void_v<T>>>
    bool isComplete() const{
        auto&jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        //実行可否
        return job.status == JobStatus::Completed;
    }

    //基本はisDoneで確認して取り出す形にする。
    template<typename T = Return_t,
        typename = std::enable_if_t<std::is_void_v<T>>>
    bool isCompleteAndGet(T* out) const {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(Id);

        if(job.status != JobStatus::Completed) return false;

        //結果を返す
        type* job = static_cast<type>(job.data);
        *out = job->result;
        return true;
    }
};

} //namespace ECS::JobSystem