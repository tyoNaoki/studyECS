#pragma once
#include "ChunkAllocator.h"

namespace ECS::JobSystem{

class ITaskQueue {


};

//T : 内部で保持する型
//Capacity : WaitJobBufferがためることができるChunkの最大数
//ChunkAllocator : Chunkを生成するクラス。
template <typename T, size_t Capacity, typename ChunkAllocator>
class TaskQueue : ITaskQueue{
    using Chunk = typename ChunkAllocator::Chunk;
    using ChunkHandle = typename ChunkAllocator::ChunkHandle;

public:
    TaskQueue(ChunkAllocator* alloc) : allocator(alloc) {}

    // マルチスレッド(Job制作用)
    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 全体のタスク数が Capacity 未満になるまで待つ
        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });

        if (!buffer_[tail_]) {
            buffer_[tail_] = allocator->allocateHandle();
            buffer_[tail_]->count = 0;
        }

        // 現在のチャンクにタスクを書き込む
        auto& chunk = buffer_[tail_];
        size_t idx = chunk->count.fetch_add(1, std::memory_order_relaxed);
        chunk->tasks[idx] = std::move(value);

        //全体のタスク数を記録
        size_.fetch_add(1, std::memory_order_release);

        //満タンになった場合、tailを進める
        if (buffer_[tail_]->full()) {
            tail_ = (tail_ + 1) % Capacity;
        }

        lock.unlock();
        not_empty_.notify_one();
    }

    // マルチスレッド
    //TaskPtr push(Job job,int degree,std::vector<TaskPtr>deps) {
    //    std::unique_lock<std::mutex> lock(mutex_);

    //    // 全体のタスク数が Capacity 未満になるまで待つ
    //    not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });

    //    if (!buffer_[tail_]) {
    //        buffer_[tail_] = ChunkPtr(
    //            allocator->allocate(),
    //            ChunkDeleter{ allocator }
    //        );
    //        buffer_[tail_]->count = 0;
    //    }

    //    // 現在のチャンクにタスクを書き込む
    //    auto& chunk = buffer_[tail_];
    //    size_t idx = chunk->count.fetch_add(1, std::memory_order_relaxed);
    //    chunk->tasks[idx] = std::move(value);

    //    //全体のタスク数を記録
    //    size_.fetch_add(1, std::memory_order_release);

    //    //満タンになった場合、tailを進める
    //    if (buffer_[tail_]->full()) {
    //        tail_ = (tail_ + 1) % Capacity;
    //    }

    //    lock.unlock();
    //    not_empty_.notify_one();
    //}

    void push(ChunkHandle&& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        size_t taskSize = value->count - value->start;

        not_full_.wait(lock, [this, &taskSize] { return (size_.load(std::memory_order_acquire) + taskSize) < Capacity; });

        ASSERT(value, "WaitJobBuffer push ChunkPtr&&value is nullptr");

        size_.fetch_add(taskSize, std::memory_order_release);

        if (buffer_[tail_] == nullptr || buffer_[tail_]->empty()) {
            //nullのスロットに直接代入
            buffer_[tail_] = std::move(value);
            if (buffer_[tail_]->full()) {
                tail_ = (tail_ + 1) % Capacity;
            }
            lock.unlock();
            not_empty_.notify_one();

            return;
        }

        //not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity-1; });
        //次のスロットに入れる。
        tail_ = (tail_ + 1) % Capacity;
        buffer_[tail_] = std::move(value);

        //入れたChunkが満タンなら
        if (buffer_[tail_]->full()) {
            tail_ = (tail_ + 1) % Capacity;
        }

        lock.unlock();
        not_empty_.notify_one();
    }

    bool try_pop(ChunkHandle& chunkPtr) {
        if (size_.load(std::memory_order_acquire) == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (size_.load(std::memory_order_acquire) == 0) {
            return false;
        }

        ASSERT(buffer_[head_], "buffer_[head_] is nullptr");
        chunkPtr = std::move(buffer_[head_]);

        buffer_[head_] = nullptr;

        if (tail_ > head_) {
            head_ = (head_ + 1) % Capacity;
        }

        size_t taskSize = chunkPtr->count.load() - chunkPtr->start.load();
        size_.fetch_sub(taskSize, std::memory_order_release);

        ASSERT(size_ >= 0, "WaitJobBuffer is size_ < 0");

        not_full_.notify_one();
        return true;
    }

    // pop（単一スレッド専用）
    //bool try_pop(T& value) {
    //    if (size_.load(std::memory_order_acquire) == 0) {
    //        return false;
    //    }

    //    //value = std::move(buffer_[head_]);
    //    head_ = (head_ + 1) % Capacity;
    //    size_.fetch_sub(1, std::memory_order_release);
    //   
    //    not_full_.notify_one();
    //    return true;
    //}

    bool wait_and_pop(ChunkHandle& chunkPtr) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] { return size_.load(std::memory_order_acquire) > 0; });

        chunkPtr = std::move(buffer_[head_]);
        buffer_[head_] = nullptr;

        head_ = (head_ + 1) % Capacity;

        size_t taskSize = chunkPtr->count.load() - chunkPtr->start.load();
        size_.fetch_sub(taskSize, std::memory_order_release);

        lock.unlock();
        not_full_.notify_one();
        return true;
    }

private:
    ChunkAllocator* allocator;
    std::array<ChunkHandle, Capacity> buffer_{};

    size_t head_ = 0, tail_ = 0;
    //全体のタスク数
    std::atomic<size_t> size_{ 0 };
    std::atomic<size_t> chunkSize_{ 0 };

    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

}