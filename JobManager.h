#pragma once
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <future>
#include <array>
#include "JobRecorder.h"
#include "JobDeque.hpp"
#include <utility>
#include "JobBarrier.h"
#include "JobDebugger.h"
#include "TaskQueue.h"
#include "taskPtr.hpp"
#include "JobWorker.h"
#include "HashFunctions.hpp"

namespace ECS::JobSystem{

    static constexpr size_t NumCategories = static_cast<size_t>(JobCategory::Num);

    /// <summary>
    /*pending	キューに積まれ、まだ取得・実行が始まっていないジョブ数	pushBottom() 後、popOrSteal() 前
      running	ワーカーが取得し、現在実行中のジョブ数   popOrSteal() 成功直後 ～ 完了まで
      completed	実行が終わり、後片付けや結果格納も含めて完了したジョブ数	runChunk / runJob() 実行後*/
    /// </summary>
    struct JobStats {

        size_t pendingJobCount(const JobCategory cat) const noexcept{
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
        }

        // pending の増減
        void onEnqueued(const JobCategory cat,size_t count) noexcept {
            pending_[size_t(cat)].fetch_add(count, std::memory_order_release);
        }

        void onDequeued(const JobCategory cat,size_t count) noexcept {
            size_t c = pending_[size_t(cat)].fetch_sub(count, std::memory_order_release);
        
            if(c < count){
                std::printf("pending count %zu, sub count is %zu\n",c,count);
            }
        }

        // running の増減
        void onStart(const JobCategory cat,size_t count) noexcept {
            running_[size_t(cat)].fetch_add(count, std::memory_order_release);
        }

        void onFinish(const JobCategory cat,size_t count) noexcept {
            running_[size_t(cat)].fetch_sub(count, std::memory_order_release);
            completed_[size_t(cat)].fetch_add(count, std::memory_order_release);

            // pending + running が 0 なら全完了を通知
            if (pending_[size_t(cat)].load(std::memory_order_acquire) == 0 &&
                running_[size_t(cat)].load(std::memory_order_acquire) == 0)
            {
                std::lock_guard<std::mutex> lk(mtx_);
                cv_.notify_all();
            }
        }

        // フレーム終端で指定カテゴリがすべて完了するまで待つ
        void waitForAll(const JobCategory cat) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] {
                return pending_[size_t(cat)].load(std::memory_order_acquire) == 0
                    && running_[size_t(cat)].load(std::memory_order_acquire) == 0;
                });
        }

        void resetCompleted() noexcept {
            for (auto& f : completed_)    f.store(0);
        }

        private:
        // 各状態のカテゴリ別カウンタ
        std::array<std::atomic<size_t>, NumCategories> pending_{};
        std::array<std::atomic<size_t>, NumCategories> running_{};
        std::array<std::atomic<size_t>, NumCategories> completed_{};

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
    using JobQueue = JobDeque<SliceChunk>;

    using StealResult = JobQueue::StealResult;

    using PopResult = JobQueue::PopResult;

    using PushResult = JobQueue::PushResult;

    using RealTimeOnlyWorker = Worker<JobQueue,RealTimePolicy,JobExecutor>;

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

public:
    static JobManager& Instance() {
        static JobManager manager;
        return manager;
    }

    void Initialize(size_t threadCount,
        std::unique_ptr<TimelineRecorder> rec = nullptr);

    ~JobManager();

    void start(){
        barrier.start();
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

    IJobBase* getJob(const JobHandle&handle){
        return jobStorage.dense[handle.jobIndex].get();
    }

    template<typename T>
    ResultStorage<T>& getResultStorage() {
        static ResultStorage<T>storage;
        return storage;
    }

    template<typename T>
    T& getResult(const JobHandle& h) {
        ASSERT(h.typeId == ecs_map::type_hash<T>(),"typeId is not same");
        return getResultStorage<T>().get(h.resultIndex);
    }

    template<class T,class... Args>
    JobHandle createJob(Args&&... args){
        auto jobIndex = jobStorage.emplace<T>(args...);

        using Ret = typename T::ReturnType;
        auto& storage = getResultStorage<Ret>();
        auto resultIndex = storage.emplace();
        return JobHandle{jobIndex,ecs_map::type_hash<Ret>(),resultIndex};
    }
    
    void scheduleJobHandle(JobHandle handle){
        size_t next = getNextQueueIndex();
        //schedule
        workers[next]->testSchedule(handle);
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
    size_t getPendingJobCount(const JobCategory cat) const noexcept {
        return stats_.pendingJobCount(cat);
    }

    size_t getRunningJobCount(const JobCategory cat) const noexcept {
        return stats_.runningJobCount(cat);
    }

    size_t getCompletedJobCount(const JobCategory cat) const noexcept {
        return stats_.completedJobCount(cat);
    }

    void waitForAllRealTime() {
        stats_.waitForAll(JobCategory::RealTime);
    }

    // 任意カテゴリに対しても待機できるよう汎用版を用意
    void waitForAll(JobCategory cat) {
        stats_.waitForAll(cat);
    }

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

private:
   

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

} //namespace ECS::JobSystem