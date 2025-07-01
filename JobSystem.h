#pragma once
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>

class JobSystem
{
public:
    explicit JobSystem(size_t threadCount);

private:
    void workerThreadFunction();

    void schedule(const std::function<void()>& job);

    void waitForAll() const;

private:
    std::queue<std::function<void()>> jobQueue;
    std::mutex queueMutex;

    std::vector<std::thread> workers;
    std::condition_variable condition;

    std::atomic<int>  jobCounter{ 0 };
    std::mutex        finishMutex;
    std::condition_variable finishCv;
    std::atomic<bool> stopFlag;
};

