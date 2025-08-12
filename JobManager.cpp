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
    for (size_t i = 0; i < threadCount; ++i) {
        waitQueues.push_back(std::make_unique<WaitQueue<TaskPtr>>());
    }

    localQueues.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        localQueues.emplace_back(
            std::make_unique<JobQueue>(capacity, i)
        );
    }

    // workers 初期化
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
    size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % localQueues.size();

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
    size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % waitQueues.size();

    waitQueues[idx]->push(task);
    outstanding.fetch_add(1, std::memory_order_acq_rel);
    wakeCv.notify_one();
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
                //wakeCv.notify_one();
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
                    //std::this_thread::sleep_for(std::chrono::microseconds(50));
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

void JobManager::run_pending_job(size_t queueIndex)
{
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

    //他スレッドからsteal
    //Block時、steal再挑戦にする
    {
        auto stealRes = stealFromOthers(queueIndex);

        if (stealRes.status == StealStatus::Success) {
            runJob(queueIndex, std::move(stealRes.value));
            return;
        }
    }

    /*if (!globalQueue.empty()) {

        std::optional<TaskPtr> fallback;
        {
            std::lock_guard lk(globalQueueMtx);

            if (!globalQueue.empty()) {
                fallback = globalQueue.back();
                globalQueue.pop_back();
            }
        }

        if (fallback) {
            runJob(queueIndex, std::move(*fallback));
            return;
        }
    }*/

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

}//namespace ECS::JobSystem