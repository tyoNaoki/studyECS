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
#include "JobRecorder.h"
#include "JobDeque.hpp"

namespace ECS::JobSystem{

    template<typename Recorder>
    class TestJobSystem;

    using Job = std::packaged_task<void()>;
    using JobHandle = std::shared_future<void>;

template<typename Recorder = NullRecorder>
class JobSystem
{
    using InputJob = std::function<void()>;

public:
    explicit JobSystem(size_t threadCount,Recorder* rec = nullptr,size_t capacity = 1024) : recorder(rec),stopFlag(0),nextQueue(0)
    {
        jobQueues.reserve(threadCount);
        for (size_t i = 0; i < threadCount; i++) {
            jobQueues.emplace_back(std::make_unique<JobDeque<Job>>(capacity));
        }

        workers.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this, i]()noexcept {
                this->workerThreadFunction(i);
                });
        }
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

    //通常Job追加
    JobHandle schedule(const InputJob& job){
        std::packaged_task<void()> task(job);
        auto future = task.get_future().share();

        //未処理カウンタ増加
        outstanding.fetch_add(1, std::memory_order_acq_rel);
        size_t idx = nextQueue.fetch_add(1, std::memory_order_relaxed) % jobQueues.size();

        {
            std::lock_guard lk(wakeMutex);

            //ローカルバッファにpush
            jobQueues[idx]->pushBottom(std::move(task));

            std::cout << "[START] queue=" << idx << " outstanding=" << outstanding << std::endl;
            wakeCv.notify_one();
        }

        return future;
    }

    //debug付きJob追加
    JobHandle schedule(char name,const InputJob& job) {

        //ジョブをラップして記録
        auto wrapped = [this, name, job = std::move(job)]() mutable {
            auto h = recorder ? recorder->recordStart(name) : 0;
            job();

            if (recorder) recorder->recordEnd(h);
        };

        return schedule(wrapped);
    }

    //依存関係付きJob
    JobHandle schedule(char name,const InputJob& job,
        const std::vector<JobHandle>& dependencies) {

        auto wrapper = [this,name,job, dependencies]() mutable{
            // 全ての依存ジョブが終わるまで待機する
            for (auto& dep : dependencies) {
                if (dep.valid()) {
                    dep.wait();
                }
            }

            auto h = recorder ? recorder->recordStart(name) : 0;
            job();

            if (recorder) recorder->recordEnd(h);
        };

       return schedule(wrapper);
    }

    std::vector<JobHandle> schedule(uint32_t jobCount,char name,
        const std::function<void(uint32_t)>& job) {
        std::vector<JobHandle> handles(jobCount);
        for (uint32_t jobIndex = 0; jobIndex < jobCount; jobIndex++) {
            auto wrapper = [this,name,job, jobIndex]() {
                auto h = recorder ? recorder->recordStart(name) : 0;
                job(jobIndex);

                if (recorder) recorder->recordEnd(h);
            };

            handles[jobIndex] = schedule(wrapper);
        }

        return handles;
    }

    void waitForAll() {
        std::unique_lock<std::mutex> lk(finishMutex);
        finishCv.wait(lk, [&] {
            return outstanding.load(std::memory_order_acquire) == 0;
            });
    }

    void workerThreadFunction(size_t queueIndex) {
        const size_t n = jobQueues.size();

        // 終了フラグと outstanding の組み合わせでループ制御
        while (true) {
            //停止指示かつ未完了ジョブなしなら抜ける
            if (stopFlag.load(std::memory_order_acquire) &&
                outstanding.load(std::memory_order_acquire) == 0)
            {
                break;
            }

            std::optional<Job> opt;

            //自キューから pop
            if (auto p = jobQueues[queueIndex]->popBottom()) {
                opt = std::move(p);
            }
            else {
                //取れなければ他キューから steal
                for (size_t i = 1; i < n; ++i) {
                    size_t idx = (queueIndex + i) % n;
                    if (auto s = jobQueues[idx]->stealTop()) {
                        opt = std::move(s);
                        break;
                    }
                }
            }

            //どちらも取れなければ一旦 yield
            if (!opt) {
                std::this_thread::yield();
                continue;
            }

            //取得できたジョブを実行
            Job job = std::move(*opt);
            job();

            //完了カウンタを減らし、最後なら通知
            if (outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(finishMutex);
                std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding << std::endl;

                finishCv.notify_all();
            }else{
                std::lock_guard<std::mutex> lk(finishMutex);
                std::cout << "[FINISH] queue=" << queueIndex << " outstanding=" << outstanding << std::endl;
            }
        }

        // シャットダウン時に残った自キューのジョブを掃く
       while (auto p = jobQueues[queueIndex]->popBottom()) {
            (*p)();
       }
    }

private:

    bool popBottom(size_t queueIndex,std::optional<Job>& out){
        
        auto pop = jobQueues[queueIndex]->popBottom();
        if(pop){
            out = std::move(pop);
            return true;
        }
        
        return false;
    }

    // stealTop を使って他ワーカーから奪う
    bool stealFromOthers(size_t stealOwner, std::optional<Job>& out) {
        size_t n = jobQueues.size();

        for (size_t i = 1; i < n; ++i) {
            size_t idx = (stealOwner + i) % n;
            if (auto steal = jobQueues[idx]->stealTop()) {
                out = std::move(steal);
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
    std::vector<std::unique_ptr<JobDeque<Job>>> jobQueues;

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