#pragma once
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include "JobRecorder.h"

namespace ECS::Job{

template<typename Recorder = NullRecorder>
class JobSystem
{
public:
    explicit JobSystem(size_t threadCount,Recorder* rec = nullptr ) : recorder(rec)
    {
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this] { this->workerThreadFunction(); });
        }
    }

    ~JobSystem(){
        stopFlag.store(true, std::memory_order_relaxed);
        condition.notify_all();           // ワーカー全員を起こす

        for (auto& w : workers) {
            if (w.joinable())
                w.join();
        }
    }

    void workerThreadFunction(){
        // 無限ループで待機と実行を繰り返す
        while (true) {
            std::function<void()> job; // ジョブの入れ物を用意
            {
                // ジョブキューにジョブが追加されるまで待機する
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [&] { return !jobQueue.empty() || stopFlag.load(); });

                if (stopFlag.load() && jobQueue.empty())
                    break;

                // ジョブキューからジョブを取得する
                job = std::move(jobQueue.front());
                jobQueue.pop();
            } // ロックを解放

            // ジョブを実行する
            job();

            // デクリメント	    
            if (jobCounter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                // カウントが0になったら完了フラグを立てて通知
                std::lock_guard<std::mutex> lk(finishMutex);
                finishCv.notify_all();
            }
        }
    }

    void schedule(char name,const std::function<void()>& job){

        auto wrapped = [this, name, job]() {
            auto handle = recorder ? recorder->recordStart(name) : 0;

            // 2) 本体実行
            job();

            // 3) ジョブ終了を記録
            if(recorder) recorder->recordEnd(handle);
        };

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            jobCounter.fetch_add(1); // インクリメント

            // ジョブをキューにプッシュ
            jobQueue.push(std::move(wrapped));
        }

        // 待機中のワーカースレッドをひとつ起こす
        condition.notify_one();
    }

    void waitForAll(){
        std::unique_lock<std::mutex> lk(finishMutex);
        finishCv.wait(lk, [&] {
            return jobCounter.load(std::memory_order_acquire) == 0;
            });
    }

private:
    std::queue<std::function<void()>> jobQueue;
    std::mutex queueMutex;

    std::vector<std::thread> workers;
    std::condition_variable condition;

    std::atomic<int>  jobCounter{ 0 };
    std::mutex        finishMutex;
    std::condition_variable finishCv;
    std::atomic<bool> stopFlag;

    Recorder* recorder;
    inline static NullRecorder nullrecorder;
};

} //namespace ECS::Job