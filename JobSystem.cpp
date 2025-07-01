#include "JobSystem.h"

JobSystem::JobSystem(size_t threadCount)
{
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back([this] { this->workerThreadFunction(); });
    }
}

void JobSystem::workerThreadFunction()
{
    // 無限ループで待機と実行を繰り返す
    while (true) {
        std::function<void()> job; // ジョブの入れ物を用意
        {
            // ジョブキューにジョブが追加されるまで待機する
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [&] { return !jobQueue.empty()|| stopFlag; });

            if(stopFlag && jobQueue.empty())break;

            // ジョブキューからジョブを取得する
            job = std::move(jobQueue.front());
            jobQueue.pop();
        } // ロックを解放

    // ジョブを実行する
        job();

        // デクリメント	    
        if (jobCounter.fetch_sub(1,std::memory_order_acq_rel) == 1) {
            // カウントが0になったら完了フラグを立てて通知
            std::lock_guard<std::mutex> lk(finishMutex);
            finishCv.notify_all();
        }
    }
}

void JobSystem::schedule(const std::function<void()>& job)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobCounter.fetch_add(1); // インクリメント

        // ジョブをキューにプッシュ
        jobQueue.push(job);
    }

    // 待機中のワーカースレッドをひとつ起こす
    condition.notify_one();
}

void JobSystem::waitForAll() const
{
    std::unique_lock<std::mutex> lk(finishMutex);
    finishCv.wait(lk, [&] {
        return jobCounter.load(std::memory_order_acquire) == 0;
        });
}
