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
#include "JobQueue.hpp"

namespace ECS::JobSystem{

template<typename Recorder = NullRecorder>
class JobSystem
{
    using Job = std::function<void()>;

public:
    explicit JobSystem(size_t threadCount,Recorder* rec = nullptr,size_t capacity = 1024) : recorder(rec),stopFlag(0),nextQueue(0)
    {
        jobQueues.reserve(threadCount);
        for (size_t i = 0; i < threadCount; i++) {
            jobQueues.emplace_back(std::make_unique<ChaseLevDeque<Job>>(capacity));
        }

        workers.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this, i]()noexcept {
                this->workerThreadFunction(i);
                });
        }

        int testBreak = 0;
    }

    ~JobSystem(){
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

    void schedule(char name,Job job) {
        // ジョブをラップして記録機構を入れる
        auto wrapped = [this, name, job = std::move(job)]() mutable {
            auto h = recorder ? recorder->recordStart(name) : 0;

            job();

            if (recorder) recorder->recordEnd(h);
        };

        // ラウンドロビンでキューを選択（== 過去の nextQueue に依存）
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed)
            % jobQueues.size();

        // 2) 未処理カウンタ増加
        outstanding.fetch_add(1, std::memory_order_acq_rel);

        // 3) ローカルバッファにノーロックで push
        jobQueues[idx]->pushBottom(std::move(wrapped));

        // 4) ワーカーを起床
        wakeCv.notify_one();
    }

    void waitForAll() {
        std::unique_lock<std::mutex> lk(finishMutex);
        finishCv.wait(lk, [&] {
            return outstanding.load(std::memory_order_acquire) == 0;
            });
    }

    void workerThreadFunction(size_t queueIndex) {
        const size_t n = jobQueues.size();

        // 無限ループで待機と実行を繰り返す
        while (true) {
            Job job; // ジョブの入れ物を用意
            {
                std::unique_lock<std::mutex> lk(wakeMutex);
                wakeCv.wait(lk, [&] {
                    return stopFlag.load() ||
                        outstanding.load(std::memory_order_acquire) > 0;
                    });

                if (stopFlag.load() &&
                    outstanding.load(std::memory_order_acquire) == 0) {
                    break;
                }
            }

            std::optional<Job> opt;
            if (auto p = jobQueues[queueIndex]->popBottom()) {
                opt = std::move(p);
            }
            else {
                // 他スレッドからスティールしてみる
                for (size_t i = 1; i < n; ++i) {
                    size_t idx = (queueIndex + i) % n;

                    if (auto s = jobQueues[idx]->stealTop()) {
                        opt = std::move(s);
                        break;
                    }
                }
            }

            // 3) 取得できなければ一旦他スレッドへ譲ってループ
            if (!opt) {
                std::this_thread::yield();
                continue;
            }

            job = std::move(*opt);
            job();

            //完了待ち用通知
            if (outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(finishMutex);
                finishCv.notify_all();
            }
        }
    }

private:

    bool popBottom(size_t queueIndex,Job& out){
        
        auto pop = jobQueues[queueIndex]->popBottom();
        if(pop){
            out = std::move(*pop);
            return true;
        }
        
        return false;
    }

    // stealTop を使って他ワーカーから奪う
    bool stealFromOthers(size_t stealOwner, Job& out) {
        size_t n = jobQueues.size();

        for (size_t i = 1; i < n; ++i) {
            size_t idx = (stealOwner + i) % n;
            
            auto steal = jobQueues[idx]->stealTop();
            if (steal) {
                out = std::move(*steal);
                return true;
            }
        }
        return false;
    }

    bool allQueuesEmpty() const {
        for (auto& dq : jobQueues)
            if (!dq->empty()) return false;
        return true;
    }

private:
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<ChaseLevDeque<Job>>> jobQueues;

    std::atomic<bool> stopFlag;
    std::atomic<size_t> outstanding{ 0 };

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::atomic<size_t>      nextQueue{ 0 };    
    std::condition_variable condition;
    std::mutex        finishMutex;
    std::condition_variable finishCv;

    Recorder* recorder;
    inline static NullRecorder nullrecorder;
};

} //namespace ECS::Job