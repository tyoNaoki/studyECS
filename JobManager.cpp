#include "JobManager.h"
#include "taskPtr.hpp"
#include "JobDeque.hpp"

namespace ECS::JobSystem{

void JobManager::Initialize(size_t threadCount, std::unique_ptr<TimelineRecorder> rec, size_t capacity)
{
    ASSERT(threadCount > 0, "JobSystem is ThreadCount <= 0");
    initFlag = true;

    recorder = std::move(rec);
    stopFlag = false;
    nextQueue = 0;
    threadSize = threadCount;

    outstanding = 0;
    realTimeJobCounter = 0;
    backGroundCounter = 0;

    realTimeWaitQueues.reserve(threadCount);
    backGroundWaitQueues.reserve(threadCount);
    realTimeLocalQueue.reserve(threadCount);

    //初期化
    for (size_t i = 0; i < threadCount; ++i) {
        realTimeWaitQueues.push_back(std::make_unique<WaitQueue<TaskPtr>>());
        backGroundWaitQueues.push_back(std::make_unique<WaitQueue<TaskPtr>>());
        realTimeLocalQueue.emplace_back(
            std::make_unique<JobQueue>(capacity, i)
        );
        backGroundLocalQueue.emplace_back(
            std::make_unique<JobQueue>(capacity, i)
        );
    }

    // workers 初期化
    workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back([this, i]() noexcept {

            this->workerThreadFunction(i);
            });
    }
}
JobManager::~JobManager()
{
    if (!initFlag)return;

    stopFlag.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(wakeMutex);
        wakeCv.notify_all();           // ワーカー全員を起こす
    }

    for (auto& w : workers) {
        if (w.joinable())
            w.join();
    }
}
void JobManager::waitForAll()
{
    std::unique_lock<std::mutex> lk(finishMutex);
    finishCv.wait(lk, [&] {
        return abortFlag.load(std::memory_order_acquire)
            || outstanding.load(std::memory_order_acquire) == 0;
        });
}

void JobManager::waitForRealTimeJobAll()
{
    std::unique_lock<std::mutex> lk(realTimeJob_Mutex);

    realTimeJob_WaitCv.wait(lk, [&] {
        return abortFlag.load(std::memory_order_acquire)
            || realTimeJobCounter.load(std::memory_order_acquire) == 0;
        });
}

void JobManager::workerThreadFunction(size_t queueIndex)
{
    const size_t index = queueIndex;
    // 終了フラグと outstanding の組み合わせでループ制御
    while (true) {
        //停止指示または未完了ジョブなしなら抜ける
        if (abortFlag.load(std::memory_order_acquire)) {
            break;
        }

        if (stopFlag.load(std::memory_order_acquire) &&
            outstanding.load(std::memory_order_acquire) == 0)
        {
            break;
        }

        run_realTimeQueue(queueIndex);

        if (realTimeLocalQueue[index]->isAbort()||backGroundLocalQueue[index]->isAbort()) {
            abort();
            return;
        }
    }

    if (abortFlag.load(std::memory_order_acquire)) {
        run_while_validQueue(index);
    }
}

bool JobManager::checkRanAllJobInJobQueues()
{
    for (auto& queue : realTimeLocalQueue) {
        if (queue->validCheck()) {
            return false;
        }
    }

    return true;
}

void JobManager::pushJobWaitQueue(TaskPtr task)
{
    switch (task->category)
    {
    case JobCategory::RealTime:
        pushRealTimeJobWaitQueue(task);
        break;
    case JobCategory::BackGround:
        pushBackGroudGlobalQueue(task);
        break;
    default:
        break;
    }
}

void JobManager::pushRealTimeJobWaitQueue(TaskPtr task)
{
    //カウンター処理
    outstanding.fetch_add(1, std::memory_order_acq_rel);
    realTimeJobCounter.fetch_add(1, std::memory_order_acq_rel);

    size_t idx = getNextQueueIndex();
    realTimeWaitQueues[idx]->push(task);

    realTimeJob_WaitCv.notify_one();
}

void JobManager::pushBackGroudGlobalQueue(TaskPtr task)
{
    //カウンタ処理
    outstanding.fetch_add(1, std::memory_order_acq_rel);
    backGroundCounter.fetch_add(1, std::memory_order_acq_rel);

    std::lock_guard<std::mutex>lk(backGroundMutex);

    if (globalBackGroudQueue.capacity() < globalBackGroudQueue.size() + 1) {
        globalBackGroudQueue.reserve(globalBackGroudQueue.size() + 16); 
    }
    
    globalBackGroudQueue.push_back(std::move(task));
}

void JobManager::pushBackGroudGlobalQueue(std::vector<TaskPtr>&& tasks)
{
    const size_t size = tasks.size();

    // タスク数を加算
    outstanding.fetch_add(size, std::memory_order_acq_rel);
    backGroundCounter.fetch_add(size, std::memory_order_acq_rel);

    std::lock_guard<std::mutex>lk(backGroundMutex);

    globalBackGroudQueue.reserve(globalBackGroudQueue.size() + size);

    // move で一括挿入
    globalBackGroudQueue.insert(globalBackGroudQueue.end(),
        std::make_move_iterator(tasks.begin()),
        std::make_move_iterator(tasks.end()));
}

bool JobManager::pushBottom(TaskPtr task, std::unique_ptr<JobQueue>& localQueue, std::unique_ptr<WaitQueue<TaskPtr>>& waitQueue)
{
    auto start = std::chrono::steady_clock::now();
    const auto  timeout = std::chrono::milliseconds(2);

    auto& queue = localQueue;
    auto& waitQ = waitQueue;

    while (true) {
        PushResult res = queue->pushBottom(std::move(task));

        switch (res.status) {
            case PushStatus::Success:
            {
                return true;
            }

            case PushStatus::WouldBlock:
            {
                if (std::chrono::steady_clock::now() - start >= timeout) {
                    fallbackWaitQueue(waitQ, std::move(res.notPushed));
                    return false;
                }

                task = std::move(res.notPushed);
                // 軽めのバックオフ
                std::this_thread::yield();
                break;
            }
            case PushStatus::Full:
            {
                if (std::chrono::steady_clock::now() - start < timeout) {
                    task = std::move(res.notPushed);
                    //少し待って再挑戦
                    std::this_thread::yield();
                }
                else {
                    fallbackWaitQueue(waitQ, std::move(res.notPushed));
                    return false;
                }

                break;
            }
        }

    }
}

void JobManager::pushLocalQueue(std::unique_ptr<JobQueue>& localQueue, std::unique_ptr<WaitQueue<TaskPtr>>& waitQueue)
{
    TaskPtr task;
    while (waitQueue->try_pop(task)
        && pushBottom(std::move(task),localQueue,waitQueue)) {
    }
}

std::optional<TaskPtr> JobManager::try_popBackGroundGlobalQueue()
{
    if (globalBackGroudQueue.empty()) return std::nullopt;

    auto t = std::move(globalBackGroudQueue.back());
    globalBackGroudQueue.pop_back();
    return t;
}

void JobManager::popBackGroundGlobalQueue()
{
    constexpr double avg_jobTime = 1.0;
    const double max_BGJobs = calculateBGJobs(FrameTimeMs, bgRatio, avg_jobTime);

    std::lock_guard<std::mutex> lock(backGroundMutex);

    if (globalBackGroudQueue.empty()) return;

    const size_t max_pop = static_cast<size_t>(std::max(0.0, std::floor(max_BGJobs)));
    const size_t n = std::min({ globalBackGroudQueue.size(), backGroundWaitQueues.size(), max_pop == 0 ? SIZE_MAX : max_pop });

    size_t queueIndex = 0;
    for (size_t i = 0; i < max_pop; ++i) {
        if(auto task = try_popBackGroundGlobalQueue()){
            // 現在のキューにタスクを追加
            backGroundWaitQueues[queueIndex]->push(std::move(*task));
        }else{
            break;
        }

        // インデックスを更新（次のキューに移動）
        queueIndex = (queueIndex + 1) % backGroundWaitQueues.size();
    }
}

void JobManager::run_realTimeQueue(size_t queueIndex)
{
    pushLocalQueue(realTimeLocalQueue[queueIndex], realTimeWaitQueues[queueIndex]);

    if(!pop_and_steal_Queue(
        queueIndex
        ,realTimeLocalQueue
        , [&]() { sub_realTimeJob_counter();}) 
        && !realTimeLocalQueue[queueIndex]->isAbort()){

        pushLocalQueue(backGroundLocalQueue[queueIndex], backGroundWaitQueues[queueIndex]);

        //指定回数だけ行う
        for(int run = 0; 
            run < 5 
            && !backGroundLocalQueue[queueIndex]->isAbort();
            run++){
            if(!pop_and_steal_Queue(
                queueIndex
                ,backGroundLocalQueue
                , [&]() {
                sub_backGroundJob_counter();//カウンタ処理
                })) break;
        }
    }
}

void JobManager::run_backGroundQueue(size_t queueIndex)
{
    pushLocalQueue(backGroundLocalQueue[queueIndex], backGroundWaitQueues[queueIndex]);

    if (!pop_and_steal_Queue(queueIndex, backGroundLocalQueue, [&]() {
        sub_backGroundJob_counter(); 
        })) {
        pushLocalQueue(realTimeLocalQueue[queueIndex], realTimeWaitQueues[queueIndex]);

        pop_and_steal_Queue(queueIndex, realTimeLocalQueue, [&]() {
            sub_realTimeJob_counter();  // リアルタイム用カウンタ処理
            });
    }
}

bool JobManager::pop_and_steal_Queue(size_t queueIndex, std::vector<std::unique_ptr<JobQueue>>& queues, std::function<void()> sub_counterFunc)
{
    //リアルタイムJobを処理
    //自キューからPOP
    {
        auto popRes = queues[queueIndex]->popBottom();

        if (popRes.status == PopStatus::Success) {
            runJob(queueIndex, std::move(popRes.value));
            sub_counterFunc();
            return true;
        }
        else if (popRes.status == PopStatus::WouldBlock) {
            std::this_thread::yield();
            return true;
        }
    }

    {
        auto stealRes = stealQueues(queueIndex, queues);

        if(stealRes.status == StealStatus::Success){
            sub_counterFunc();
            return true;
        }
    }

    std::this_thread::yield();
    return false;
}

StealResult JobManager::stealQueues(size_t queueIndex, std::vector<std::unique_ptr<JobQueue>>& stealQueues)
{
    size_t n = stealQueues.size();

    StealResult result;
    for (size_t i = 1; i < n; ++i) {
        size_t idx = (queueIndex + i) % n;
        result = stealQueues[idx]->stealTop(queueIndex);

        if (result.status == StealStatus::Success) {
            runJob(queueIndex, std::move(result.value));
            return StealResult{StealStatus::Success,std::nullopt};
        }
    }

    return StealResult{StealStatus::Empty,std::nullopt};
}

void JobManager::runJob(size_t queueIndex, std::optional<TaskPtr>&& optTask)
{
    ASSERT(optTask, "runJob optTask is nullopt!!");

    TaskPtr task = std::move(*optTask);

    ASSERT(task && task->job.valid(), "task is invoked in JobQueue!!");

    task->job.invoke();

    //繋がっているchildの依存カウントを減らしていく
    for (TaskPtr child = task->nextDependent; child; child = child->nextDependent) {
        if (child->inDegree.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            pushJobWaitQueue(child);
        }
    }

    static std::mutex logMutex;

    // runJob 内
    auto prev = outstanding.fetch_sub(1, std::memory_order_acq_rel);
    bool didAllFinish = (prev == 1);

    if (didAllFinish) {
        std::lock_guard<std::mutex> lk(finishMutex);
        finishCv.notify_all();
    }

    {
        std::lock_guard<std::mutex> lk2(logMutex);
        if (didAllFinish) {
            test::saveLog("[All FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
            std::printf("[All FINISH] queue=%zu outstanding=%zu \n", queueIndex, outstanding.load());
        }
        else {
            test::saveLog("[FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
            std::printf("[FINISH] queue=%zu outstanding=%zu \n", queueIndex, outstanding.load());
        }
    }
}

void JobManager::run_while_validQueue(size_t queueIndex)
{
    TaskPtr task;
    //待機キュー内のJob処理
    while (realTimeWaitQueues[queueIndex]->try_pop(task))
    {
        runJob(queueIndex, std::move(task));
    }

    while (backGroundWaitQueues[queueIndex]->try_pop(task))
    {
        runJob(queueIndex, std::move(task));
    }

    while (true) {
        auto popRes = realTimeLocalQueue[queueIndex]->popBottom();

        if (popRes.status == PopStatus::Success) {
            runJob(queueIndex, std::move(popRes.value));
            continue;
        }

        if (popRes.status == PopStatus::Empty) {
            break;
        }
    }

    while (true) {
        auto popRes = backGroundLocalQueue[queueIndex]->popBottom();

        if (popRes.status == PopStatus::Success) {
            runJob(queueIndex, std::move(popRes.value));
            continue;
        }

        if (popRes.status == PopStatus::Empty) {
            break;
        }
    }
}

bool JobManager::allQueuesEmpty() const
{
    for (auto& dq : realTimeLocalQueue)
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
    return nextQueue.fetch_add(1, std::memory_order_relaxed) % realTimeWaitQueues.size();
}

double JobManager::calculateBGJobs(double target_ms, double bgRatio, double avgJobTime)
{
    double bg_budget_ms = target_ms * bgRatio;  // BG用の時間
    return bg_budget_ms / avgJobTime;            // 処理可能なジョブ数
}

void JobManager::sub_realTimeJob_counter()
{
    if (realTimeJobCounter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lk(realTimeJob_Mutex);
        realTimeJob_WaitCv.notify_all();
    }
}

void JobManager::sub_backGroundJob_counter()
{
    backGroundCounter.fetch_sub(1, std::memory_order_acq_rel);
}

}//namespace ECS::JobSystem