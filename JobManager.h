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

        std::atomic<JobStatus> status{JobStatus::UnSchedule};

        JobEntry() = default;
        JobEntry(Invoker f, IJobBase* d) : func(f), data(d) {}
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
            removeJobs.clear();
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


            ASSERT(sparse[getJodIndex(newId)] == NULL_JOB_ID,"valid sparse slot do not use");

            auto* p = new DerivedJob(std::forward<Args>(args)...);
            const size_t newIndex = jobData.size();
            jobData.emplace_back(nullptr,std::move(p));
            jobIds.push_back(newId);

            sparse[getJodIndex(newId)] = newIndex;

            return newId;
        }

        //schedule時に対応ジョブに関数ポインターを割り当てる
        template<class DerivedJob>
        void createJobFunction(const JobId& id){
            jobData[getJobIndex(id)].func = &Invoke<DerivedJob>;
        }

        void addRemoveJobs(std::vector<JobHandle>&& jobs) {
            removeJobs.insert(removeJobs.end(),
                std::move_iterator(jobs.begin()),
                std::move_iterator(jobs.end()));
        }

        //GetJobEntry関数の参照が壊れるので、絶対にFrameの最後全てのジョブを処理か、処理をしていないタイミングで行うこと!!
        void removeJobsOnLastFrame() {
            std::lock_guard<std::mutex> lk(lock);

            std::sort(removeJobs.begin(), removeJobs.end(),
                [&](JobHandle& a, JobHandle& b) {
                    return sparse[a.jobId] > sparse[b.jobId]; // removeIndex の大きい順
                });

            for (auto& removeHandle : removeJobs) {
                removeJob(removeHandle.jobId);
            }

            removeJobs.clear();
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

        std::vector<JobHandle>removeJobs;

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
    using JobQueue = JobDeque<ChunkMeta>;

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

    JobManager() = default;

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    struct Executor {
        void runJob(size_t workerId, JobHandle* handle);

        void runSlot(size_t workerId, JobHandle* begin, JobHandle* end);

        void runChunk(size_t workerId, ChunkMeta&& chunk);

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

    auto& getJobEntry(const JobId jobId){
        return jobStorage.getJobEntry(jobId);
    }

    bool containsJob(const JobId jobId) const{
        return jobStorage.containsJob(jobId);
    }

    //ここに
    template<
        typename T,
        TaskCategory TC = TaskCategory::Easy,
        JobCategory JC = JobCategory::RealTime,
        typename... Args
    >
    JobHandle createJob(Args&&... args){
        JobId id = jobStorage.emplaceJobData<T>(std::forward<Args>(args)...);
        jobStorage.createJobFunction<T>(id);
        JobHandle h{ id, TC,JC};
        return h;
    }

    template<typename T>
    TaskFuture<T>&& scheduleJobHandle(const JobHandle& jobHadnle) {

        auto&entry = getJobEntry(jobHadnle.jobId);
        //すでにスケジュール済み
        ASSERT(!entry.func,"this job is scheduled");

        //いずれ、自動でtaskCategoryを算出できるようにする

        stats_.onScheduled(jobHadnle.jobCategory,1);

        if(entry.data->inDegree.load(std::memory_order_acquire) == 0){
            enqueue(jobHadnle);
        }

        return std::move(TaskFuture<T>(jobHadnle));
    }

    void scheduleDependentHandle(JobHandle childHandle){
        enqueue(childHandle);
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

    template<typename JobT>
    void addCommand(const JobHandle&handle, std::function<void(JobT&)>&&cmd){
        auto* job = getJobEntry(handle.jobId).data;
        job->AddRequeset(std::move(cmd));
    }

    //帰り値はJobID
    //template<typename IJobClass>
    //EntityID createJob(){return 0;}

    ////帰り値はJobResultID
    //template<typename JobResult>
    //EntityID createJobResult(){return 0;}

    bool checkRanAllJobInJobQueues();

    IRecorder* getRecorder(){
        if(recorder){
            return recorder.get();
        }

        return nullptr;
    }

    //TaskPtr pushJobWaitQueue(Job&& job,int degree,JobCategory cat);

    //フレーム始めに計算した処理数のBGを各ワーカーごとのBGQueueに割り振る。
    //計算は以下パラメータを使用する。
    //パラメータ(目標FPSms時間、ワンフレームでどのくらいBGを処理するかの比率、1ジョブの平均実行ms時間)
    void popGlobalBackGroundQueue();

    const size_t getThreadSize() const{ return threadSize;}

    void setStartFrameTime();

    std::chrono::steady_clock::time_point getStartFrameTime() const;

public:
    void allFlushJob(const JobCategory category);

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

private:
    void enqueue(JobHandle handle);

    bool allQueuesEmpty() const;

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

private:
    double avg_JobTimeMs = 1.0f;
    double avg_ExecuteJobTime = 0.1;

    JobStats stats_;

    // 時刻
    std::chrono::steady_clock::time_point frameStart;

    size_t threadSize;
    bool initFlag;

    std::mutex logMutex_;
    inline static thread_local std::ostringstream localLogBuffer;

    std::vector<TaskPtr>globalBackGroudQueue;
    std::mutex backGroundMutex;

    std::vector<std::unique_ptr<JobQueue>> localQueues;

    std::vector<std::unique_ptr<IWorker>> workers;

    JobStorage jobStorage;

    std::atomic<bool> stopFlag;
    
    //現ジョブの総数
    std::atomic<size_t> outstanding{ 0 };

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::mutex            realTimeJob_Mutex;
    std::condition_variable realTimeJob_WaitCv;

    std::mutex            backGroundJob_Mutex;
    std::condition_variable backGroundJob_WaitCv;

    std::atomic<size_t>      nextQueue{ 0 };
    std::condition_variable condition;
    std::mutex        finishMutex;
    std::condition_variable finishCv;

    std::unique_ptr<TimelineRecorder> recorder;

    JobBarrier barrier;

    std::atomic<bool> abortFlag{ false };
};

template<typename Derived_t>
struct TaskFuture {
    JobHandle handle;

    using type = Derived_t;
    using Return_t = typename type::Return_t;

    explicit TaskFuture(JobHandle h) : handle(h) {}

    /// <summary>
    /// Jobにコマンド形式で関数ポインターを渡していく
    /// job.AddRequest(([&capture](JobClass& t) {
    ///     t.temp = capture;
    /// }));
    /// </summary>
    /// <returns></returns>
    void AddRequest(std::function<void(Derived_t&)> cmd) {
        JobManager::Instance().template addCommand<Derived_t>(handle.jobIndex, std::move(cmd));
    }

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
        auto& job = jm.getJobEntry(handle.jobIndex);

        //実行可否
        return !job.func;
    }

    //基本はisDoneで確認して取り出す形にする。
    template<typename T = Return_t,
        typename = std::enable_if_t<std::is_void_v<T>>>
    bool isCompleteAndGet(T* out) const {
        auto& jm = JobManager::Instance();
        auto& job = jm.getJobEntry(handle.jobIndex);

        if(job.func) return false;

        //結果を返す
        type* job = static_cast<type>(job.data);
        *out = job->result;
        return true;
    }
};

} //namespace ECS::JobSystem