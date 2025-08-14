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

    waitQueues.reserve(threadCount);
    backGroudTimeQueues.reserve(threadCount);
    localQueues.reserve(threadCount);

    //初期化
    for (size_t i = 0; i < threadCount; ++i) {
        waitQueues.push_back(std::make_unique<WaitQueue<TaskPtr>>());
        backGroudTimeQueues.push_back(std::make_unique<WaitQueue<TaskPtr>>());
        localQueues.emplace_back(
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
void JobManager::run_one_job()
{
    size_t idx = getNextQueueIndex();

    run_pending_job(idx);
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

        //ローカルキューにpushしていく。
        pushLocalQueue(index);

        //自キューから pop
        run_pending_job(index);

        if (localQueues[index]->isAbort()) {
            abort();
            return;
        }
    }

    if (abortFlag.load(std::memory_order_acquire) != false) {
        run_while_validQueue(index);
    }
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

void JobManager::pushWaitQueue(TaskPtr task)
{
    size_t idx = getNextQueueIndex();

    waitQueues[idx]->push(task);
    outstanding.fetch_add(1, std::memory_order_acq_rel);
    wakeCv.notify_one();
}

void JobManager::pushBackGroudGlobalQueue(TaskPtr&& task)
{
    if (globalBackGroudQueue.capacity() < globalBackGroudQueue.size() + 1) {
        globalBackGroudQueue.reserve(globalBackGroudQueue.size() + 16); 
    }

    globalBackGroudQueue.push_back(std::move(task));
}

void JobManager::pushBackGroudGlobalQueue(std::vector<TaskPtr>&& tasks)
{
    const size_t size = tasks.size();

    globalBackGroudQueue.reserve(globalBackGroudQueue.size() + size);

    // move で一括挿入
    globalBackGroudQueue.insert(globalBackGroudQueue.end(),
        std::make_move_iterator(tasks.begin()),
        std::make_move_iterator(tasks.end()));

    // タスク数を加算
    outstanding.fetch_add(size, std::memory_order_acq_rel);
}

bool JobManager::pushBottom(TaskPtr task, size_t idx)
{
    auto start = std::chrono::steady_clock::now();
    const auto  timeout = std::chrono::milliseconds(2);

    auto& queue = localQueues[idx];

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
                    fallbackWaitQueue(idx, std::move(res.notPushed));
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
                    fallbackWaitQueue(idx, std::move(res.notPushed));
                    return false;
                }

                break;
            }
        }

    }
}

void JobManager::pushLocalQueue(size_t queueIndex)
{
    TaskPtr task;
    while (waitQueues[queueIndex]->try_pop(task)
        && pushBottom(std::move(task), queueIndex)){}
}

std::optional<TaskPtr> JobManager::try_popBG()
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
    const size_t n = std::min({ globalBackGroudQueue.size(), backGroudTimeQueues.size(), max_pop == 0 ? SIZE_MAX : max_pop });

    size_t queueIndex = 0;
    for (size_t i = 0; i < max_pop; ++i) {
        if(auto task = try_popBG()){
            // 現在のキューにタスクを追加
            backGroudTimeQueues[queueIndex]->push(std::move(*task));
        }else{
            break;
        }

        // インデックスを更新（次のキューに移動）
        queueIndex = (queueIndex + 1) % backGroudTimeQueues.size();
    }
}

bool JobManager::try_popBackGroudQueue(size_t queueIndex)
{
    if (backGroudTimeQueues.empty())return false;
    TaskPtr task;

    while (backGroudTimeQueues[queueIndex]->try_pop(task)
        && pushBottom(std::move(task), queueIndex)) {
    }

    return true;
}

void JobManager::run_pending_job(size_t queueIndex)
{
    //リアルタイムJobを処理
    //自キューからPOP
    {
        auto popRes = localQueues[queueIndex]->popBottom();

        if (popRes.status == PopStatus::Success) {
            runJob(queueIndex, std::move(popRes.value));
            return;
        }
        else if (popRes.status == PopStatus::WouldBlock) {
            std::this_thread::yield();
            return;
        }
    }

    //BGJob処理
    if (try_popBackGroudQueue(queueIndex))return;

    //他スレッドからsteal
    //Block時、steal再挑戦にする
    {
        auto stealRes = stealFromOthers(queueIndex);

        if (stealRes.status == StealStatus::Success) {
            runJob(queueIndex, std::move(stealRes.value));
            return;
        }
    }

    //どちらも取れなければ一旦 yield
    std::this_thread::yield();
}

StealResult JobManager::stealFromOthers(size_t stealOwner)
{
    size_t n = localQueues.size();

    StealResult result;
    for (size_t i = 1; i < n; ++i) {
        size_t idx = (stealOwner + i) % n;
        result = localQueues[idx]->stealTop(stealOwner);

        if (result.status == StealStatus::Success) {
            return result;
        }
    }

    return { StealStatus::Empty, std::nullopt };
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
            //pushBottom(child);
            pushWaitQueue(child);
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
        }
        else {
            test::saveLog("[FINISH] queue=%zu outstanding=%zu", queueIndex, outstanding.load());
        }
    }
}

void JobManager::run_while_validQueue(size_t queueIndex)
{
    TaskPtr task;
    while (waitQueues[queueIndex]->try_pop(task))
    {
        runJob(queueIndex, std::move(task));
    }

    while (true) {
        auto popRes = localQueues[queueIndex]->popBottom();

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
    return nextQueue.fetch_add(1, std::memory_order_relaxed) % waitQueues.size();
}

double JobManager::calculateBGJobs(double target_ms, double bgRatio, double avgJobTime)
{
    double bg_budget_ms = target_ms * bgRatio;  // BG用の時間
    return bg_budget_ms / avgJobTime;            // 処理可能なジョブ数
}

}//namespace ECS::JobSystem