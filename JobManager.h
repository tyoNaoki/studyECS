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
#include "JobRecorder.h"
#include "JobDeque.hpp"
#include <utility>
#include "JobBarrier.h"
#include "JobDebugger.h"
#include "TaskQueue.h"
#include "taskPtr.hpp"
#include "JobWorker.h"
#include "HashFunctions.hpp"
#include "CallbackList.hpp"

namespace ECS::JobSystem{

    template<typename T>
    struct TaskFuture;

    template<>
    struct TaskFuture<void>;

    struct JobStorage {
        /*void Delete(EntityID) = 0;
        virtual void Clear() = 0;
        virtual size_t Size() const noexcept = 0;
        virtual bool ContainsEntity(const EntityID) const noexcept = 0;
        virtual std::vector<EntityID>& GetEntityList() noexcept = 0;
        virtual EntityID GetEntity(const std::size_t) const = 0;
        virtual size_t Index(EntityID) const = 0;
        virtual ecs_map::id_type Hash()const = 0;

        virtual void swap_elements(const EntityID lhs, const EntityID rhs) = 0;

        virtual void swap_elementOnly(const size_t lhs, const size_t rhs) = 0;
        virtual void swap_entityOnly(const size_t lhs, const size_t rhs) = 0;

        virtual iterator begin() noexcept = 0;
        virtual iterator end() noexcept = 0;

        virtual const_iterator begin() const noexcept = 0;
        virtual const_iterator end() const noexcept = 0;

        virtual reverse_iterator rbegin() noexcept = 0;
        virtual reverse_iterator rend() noexcept = 0;

        virtual const_reverse_iterator crbegin() const noexcept = 0;
        virtual const_reverse_iterator crend() const noexcept = 0;*/

        template<class DerivedJob, class... Args>
        size_t emplace(Args&&... args) {
            dense.emplace_back(std::make_unique<DerivedJob>(std::forward<Args>(args)...));
            return dense.size() - 1;
        }

        //Job
        std::vector<std::unique_ptr<IJobBase>>dense;
    };

    template<typename T>
    struct ResultSlot {
        std::optional<T> value;

        ResultSlot() = default;

        // コピー可能にする
        ResultSlot(const ResultSlot& other)
            : value(other.value) {}

        ResultSlot& operator=(const ResultSlot& other) {
            value = other.value;
            return *this;
        }

        // ムーブも可能にする
        ResultSlot(ResultSlot&&) noexcept = default;
        ResultSlot& operator=(ResultSlot&&) noexcept = default;
    };


    template<>
    struct ResultSlot<void> {
        std::atomic<bool> done{ false };

        ResultSlot() = default;

        // コピー可能にする
        ResultSlot(const ResultSlot& other)
            :
            done(other.done.load(std::memory_order_relaxed)) {}

        ResultSlot& operator=(const ResultSlot& other) {
            done.store(other.done.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }

        // ムーブも可能にする
        ResultSlot(ResultSlot&&) noexcept = default;
        ResultSlot& operator=(ResultSlot&&) noexcept = default;
    };

    template<typename T>
    struct ResultStorage {

        size_t emplace() {
            dense.emplace_back(); // デフォルト構築
            return dense.size() - 1;
        }

        size_t push(T&& value) {
            dense.emplace_back(ResultSlot<T>{ std::move(value) });
            return dense.size() - 1;
        }

        T get(size_t idx) { return *dense[idx].value; }

        bool contains(size_t idx) const{
            if(idx >=dense.size()) return false;

            return true;/*idxとのpairのresultを見て、存在する*/
        }

        bool done(size_t idx) const{
            return dense[idx].value != std::nullopt;
        }

        //無効なら新しいresultIndexを発行
        void tryEmplace(size_t& resultIndex){
            if(resultIndex >= dense.size()){
                resultIndex = emplace();
            }
        }

        void set(const size_t resultIndex,T&&value){
            //resultSlotがまだ未作成
            if(resultIndex >= dense.size()){
                emplace();//resultSlot作成
            }

            auto& resultSlot = dense[resultIndex];

            resultSlot.value = std::move(value);
        }

        void clearResult() {
            dense.clear();
        }

    private:
        std::vector<ResultSlot<T>> dense;
    };

    template<>
    struct ResultStorage<void> {

        size_t emplace() {
            dense.emplace_back(); // デフォルト構築
            return dense.size() - 1;
        }

        template<typename T>
        size_t push(T&& value) {
            dense.emplace_back();
            return dense.size() - 1;
        }

        bool contains(size_t idx) const {
            if (idx >= dense.size()) return false;

            return true;/*idxとのpairのresultを見て、存在する*/
        }

        bool done(size_t idx) const {
            return dense[idx].done.load(std::memory_order_acquire);
        }

        //無効なら新しいresultIndexを発行
        size_t tryEmplace(size_t oldResultIndex) {
            if (oldResultIndex >= dense.size()) {
                return emplace();
            }

            return oldResultIndex;
        }

        void set(const size_t resultIndex) {
            auto& resultSlot = dense[resultIndex];
            if (resultSlot.done.load(std::memory_order_acquire)) {
                ASSERT(false, "job is done");
            }

            resultSlot.done.store(true, std::memory_order_release);
        }

        void clearResult() {
            dense.clear();
        }

    private:
        std::vector<ResultSlot<void>> dense;
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

        void onNotifyWaitAll() noexcept{
            cv_.notify_all();
        }

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

    static constexpr size_t bufferCap = 20'000;

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

        void runSlot(TaskArena* owner, size_t workerId, size_t offset, size_t localIndex);

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

    void Initialize(size_t threadCount,
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

    //通常Job追加
    template<typename F>
    auto schedule_job(F&& func){

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                 if constexpr (std::is_void_v<R>) {
                    fn();           
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        if (t->inDegree.load() == 0) {
            pushRealTimeJobWaitQueue(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //通常Job追加(依存Task)
    template<typename F>
    auto schedule_job(F&& func,const std::vector<TaskPtr>& deps) {

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {

                if constexpr (std::is_void_v<R>) {
                    fn();            
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        for (auto &d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d&&d->job.valid()) {
                addDependent(d.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            pushRealTimeJobWaitQueue(t);
        }

        return std::make_pair(
            t, 
            future
        );
    }

    //debug付きJob追加
    template<typename F>
    auto schedule(char name,F&& func){

        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;

            fn();

            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped));
    }

    //debug付きJob追加
    template<typename F>
    auto schedule(char name, F&& func, const std::vector<TaskPtr>& deps){
    
        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;
            fn();
            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped), deps);
    }

    auto scheduleTask(TaskPtr task) {
        size_t index = getNextQueueIndex();

        if (task->category == JobCategory::BackGround) {
            ASSERT(false, "BackGroundJob not work");
            //pushBackGroudGlobalQueue(std::move(task));
        }

        return workers[index]->schedule(task->category,std::move(task));
    }

    auto scheduleTask(JobCategory cat,Job&&job,int degree){
        size_t index = getNextQueueIndex();

        if(cat == JobCategory::BackGround){
            ASSERT(false,"BackGroundJob not work");
            //pushBackGroudGlobalQueue(std::move(task));
        }

        return workers[index]->schedule(cat, std::move(job), degree);
    }

    IJobBase* getJob(const size_t jobIndex){
        return jobStorage.dense[jobIndex].get();
    }

    template<typename T>
    void setResult(const JobHandle&handle,T&&value){
        //ASSERT(h.typeId == ecs_map::type_hash<T>(),"typeId is not same");
        getResultStorage<T>().set(handle.resultIndex,std::move(value));
    }

    void setResult(const size_t resultIndex) {
        //ASSERT(h.typeId == ecs_map::type_hash<T>(),"typeId is not same");
        getResultStorage<void>().set(resultIndex);
    }

    template<typename T>
    bool doneJob(const TaskFuture<T>& future)const{
        return getResultStorage<typename T::Return_t>().done(future.handle.resultIndex);
    }

    template<typename T>
    auto& getResult(TaskFuture<T>& future)const {
        //ASSERT(h.typeId == ecs_map::type_hash<T>(),"typeId is not same");

        return getResultStorage<typename T::Return_t>().get(future.handle.resultIndex);
    }

    template<typename T,typename... Args>
    JobHandle createJob(TaskCategory taskCategory,JobCategory jobCategory,Args&&... args){
        using Ret = typename T::Return_t;

        //いずれ、自動でtaskCategoryを算出できるようにする
        auto idx = jobStorage.emplace<T>(std::forward<Args>(args)...);
        JobHandle h{ idx, ecs_map::type_hash<Ret>(), NULL_RESULT,taskCategory,jobCategory };

        return h;
    }

    bool isReadyJobHandle(const JobHandle&handle){
        auto* job = getJob(handle.jobIndex);
        return job->ready.load(std::memory_order_acquire);
    }

    template<typename T>
    auto scheduleJobHandle(JobHandle& handle){
        auto* job = getJob(handle.jobIndex);

        //すでにスケジュール済み
        ASSERT(job->ready.load(std::memory_order_acquire)!= true,"this job schedule yet.");

        stats_.onScheduled(handle.jobCategory,1);

        //結果スロットをバインド
        auto& storage = getResultStorage<typename T::Return_t>();
        handle.resultIndex = storage.emplace();

        job->ready.store(true, std::memory_order_release);

        if(job->inDegree.load(std::memory_order_acquire) == 0){
            enqueue(handle);
        }

        return TaskFuture<T>(handle);
    }

    template<typename T>
    void addDependent(JobHandle&childHandle,TaskFuture<T> parent){
        auto& jm = JobManager::Instance();

        auto parentIndex = parent.handle.jobIndex;
        ASSERT(childHandle.jobIndex != parentIndex,"addDependent() do not use TaskFuture<T> of childHandle");

        auto* child = jm.getJob(childHandle.jobIndex);
        auto* parent = jm.getJob(parentIndex);

        std::lock_guard<std::mutex> lk(parent->dependentLock);
        if (!parent.isDone()) { // まだ実行されていない
            // childを親のnextDependentに差し込む
            child->nextDependent = parent->nextDependent;
            parent->nextDependent = childHandle;

            // 子ジョブの未解決依存数を増やす
            child->inDegree.fetch_add(1);
        }
    }

    template<typename JobT>
    void addCommand(const JobHandle&handle, std::function<void(JobT&)>cmd){
        auto* job = static_cast<JobT*>(getJob(handle));
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
    void allFlushJob(const JobCategory category) {
        //全フラッシュ
        for (int i = 0; i < workers.size(); i++) {
            workers[i]->flush(category);
        }
    }

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    //結果記録用ストレージを取得
    template<typename T>
    ResultStorage<T>& getResultStorage() {
        static ResultStorage<T>storage;
        //登録
        getOrRegisterStorageHandle(storage);
        return storage;
    }

    template<>
    ResultStorage<void>& getResultStorage<void>() {
        static ResultStorage<void> storage;
        //登録
        getOrRegisterStorageHandle(storage);

        return storage;
    }

    //取得、未登録なら登録する
    template<typename T>
    auto& getOrRegisterStorageHandle(ResultStorage<T>&storage){

        static auto handle = clearResultCallbacks.append([&] {
            storage.clearResult();
            });

        return handle;
    }

    //結果記録ストレージを削除する
    //もう使わないであろうストレージを削除する
    template<typename T>
    void unregisterStorage() {
        auto& storage = getResultStorage<T>();
        auto& handle = getOrRegisterStorageHandle<T>(storage);
        clearResultCallbacks.remove(handle);
        handle.reset();
    }

private:
    void enqueue(JobHandle handle){
        size_t next = getNextQueueIndex();

        workers[next]->enqueue(handle);
        
        //waitForAllで待機しているスレッドを呼び出し
        stats_.onNotifyWaitAll();
    }

    bool allQueuesEmpty() const;

    void addDependent(Task* parent, Task* child) {
        // child を親の先頭に差し込む
        child->nextDependent = parent->nextDependent;
        parent->nextDependent = child;
        child->inDegree.fetch_add(1);
    }

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

    //resultStorage消去用
    EVENT::CallbackList<void()> clearResultCallbacks;

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
    JobHandle& handle;

    using type = Derived_t;
    using Return_t = typename type::Return_t;
    using ICommandPtr = std::unique_ptr<ICommand<Derived_t>>;

    explicit TaskFuture(JobHandle& h) : handle(h) {}

    /// <summary>
    /// job.AddRequest(std::make_unique<ECS::JobSystem::FuncCmd<JobClass>>([&capture](JobClass& t) {
    ///     t.temp = capture;
    /// }));
    /// </summary>
    /// <returns></returns>
    void AddRequest(ICommandPtr&& cmd) {
        JobManager::Instance().template addCommand<Derived_t>(handle, std::move(cmd));
    }

    Return_t wait_and_get() const {
        auto index = handle.resultIndex;
        auto& resultStorage = JobManager::Instance().template getResultStorage<Return_t>();

        if (!resultStorage.contains(index)) {
            ASSERT(false, "this handle is not bind result");

            if constexpr (!std::is_void_v<Return_t>)
                return Return_t{};
            else
                return; // void の場合は return だけ
        }

        while (true) {
            if (resultStorage.done(index)) {
                if constexpr (!std::is_void_v<Return_t>) {
                    return resultStorage.get(index);
                }
                else {
                    // void の場合は値を返さず終了
                    return;
                }
            }
            else {
                std::this_thread::yield();
            }
        }
    }

    bool isDone() const {
        return JobManager::Instance().template getResultStorage<Return_t>().done(handle.resultIndex);
    }
};

} //namespace ECS::JobSystem