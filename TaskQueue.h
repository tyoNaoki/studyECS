#pragma once
#include "taskPtr.hpp"

namespace ECS::JobSystem{

class ITaskQueue {};

// ディスクリプタ：バッファ上の [start, start+count) を表す


////T : 内部で保持する型
////Capacity : WaitJobBufferがためることができるChunkの最大数
////ChunkAllocator : Chunkを生成するクラス。
//template <typename T, size_t Capacity, typename ChunkAllocator>
//class TaskQueue : ITaskQueue{
//    using Chunk = typename ChunkAllocator::Chunk;
//    using ChunkHandle = typename ChunkAllocator::ChunkHandle;
//
//    // ディスクリプタ：バッファ上の [start, start+count) を表す
//    struct SliceChunk {
//        size_t start;
//        size_t count;
//
//    };
//
//public:
//    TaskQueue(ChunkAllocator* alloc) : allocator(alloc) {}
//
//    // マルチスレッド(Job制作用)
//    void push(const T& value) {
//        std::unique_lock<std::mutex> lock(mutex_);
//
//        // 全体のタスク数が Capacity 未満になるまで待つ
//        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });
//
//        if (!buffer_[tail_]) {
//            buffer_[tail_] = allocator->allocateHandle();
//            buffer_[tail_]->count = 0;
//        }
//
//        // 現在のチャンクにタスクを書き込む
//        auto& chunk = buffer_[tail_];
//        size_t idx = chunk->count.fetch_add(1, std::memory_order_relaxed);
//        chunk->tasks[idx] = std::move(value);
//
//        //全体のタスク数を記録
//        size_.fetch_add(1, std::memory_order_release);
//
//        //満タンになった場合、tailを進める
//        if (buffer_[tail_]->full()) {
//            tail_ = (tail_ + 1) % Capacity;
//        }
//
//        lock.unlock();
//        not_empty_.notify_one();
//    }
//
//    //マルチスレッド
//    TaskPtr push(Job&& job,int degree,std::vector<TaskPtr>deps) {
//        std::unique_lock<std::mutex> lock(mutex_);
//
//        // 全体のタスク数が Capacity 未満になるまで待つ
//        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });
//
//        if (!buffer_[tail_]) {
//            buffer_[tail_] = ChunkPtr(
//                allocator->allocate(),
//                ChunkDeleter{ allocator }
//            );
//            buffer_[tail_]->count = 0;
//        }
//
//        // 現在のチャンクにタスクを書き込む
//        auto& chunk = buffer_[tail_];
//        size_t idx = chunk->count.fetch_add(1, std::memory_order_relaxed);
//        chunk->tasks[idx] = ;
//
//        //全体のタスク数を記録
//        size_.fetch_add(1, std::memory_order_release);
//
//        //満タンになった場合、tailを進める
//        if (buffer_[tail_]->full()) {
//            tail_ = (tail_ + 1) % Capacity;
//        }
//
//        lock.unlock();
//        not_empty_.notify_one();
//    }
//
//    void enqueue(ChunkHandle&& value) {
//        std::unique_lock<std::mutex> lock(mutex_);
//
//        size_t taskSize = value->count - value->start;
//
//        not_full_.wait(lock, [this, &taskSize] { return (size_.load(std::memory_order_acquire) + taskSize) < Capacity; });
//
//        ASSERT(value, "WaitJobBuffer push ChunkPtr&&value is nullptr");
//
//        size_.fetch_add(taskSize, std::memory_order_release);
//
//        if (buffer_[tail_] == nullptr || buffer_[tail_]->empty()) {
//            //nullのスロットに直接代入
//            buffer_[tail_] = std::move(value);
//            if (buffer_[tail_]->full()) {
//                tail_ = (tail_ + 1) % Capacity;
//            }
//            lock.unlock();
//            not_empty_.notify_one();
//
//            return;
//        }
//
//        //not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity-1; });
//        //次のスロットに入れる。
//        tail_ = (tail_ + 1) % Capacity;
//        buffer_[tail_] = std::move(value);
//
//        //入れたChunkが満タンなら
//        if (buffer_[tail_]->full()) {
//            tail_ = (tail_ + 1) % Capacity;
//        }
//
//        lock.unlock();
//        not_empty_.notify_one();
//    }
//
//    bool try_pop(ChunkHandle& chunkPtr) {
//        if (size_.load(std::memory_order_acquire) == 0) {
//            return false;
//        }
//
//        std::lock_guard<std::mutex> lock(mutex_);
//
//        if (size_.load(std::memory_order_acquire) == 0) {
//            return false;
//        }
//
//        ASSERT(buffer_[head_], "buffer_[head_] is nullptr");
//        chunkPtr = std::move(buffer_[head_]);
//
//        buffer_[head_] = nullptr;
//
//        if (tail_ > head_) {
//            head_ = (head_ + 1) % Capacity;
//        }
//
//        size_t taskSize = chunkPtr->count.load() - chunkPtr->start.load();
//        size_.fetch_sub(taskSize, std::memory_order_release);
//
//        ASSERT(size_ >= 0, "WaitJobBuffer is size_ < 0");
//
//        not_full_.notify_one();
//        return true;
//    }
//
//    // pop（単一スレッド専用）
//    //bool try_pop(T& value) {
//    //    if (size_.load(std::memory_order_acquire) == 0) {
//    //        return false;
//    //    }
//
//    //    //value = std::move(buffer_[head_]);
//    //    head_ = (head_ + 1) % Capacity;
//    //    size_.fetch_sub(1, std::memory_order_release);
//    //   
//    //    not_full_.notify_one();
//    //    return true;
//    //}
//
//    bool wait_and_pop(ChunkHandle& chunkPtr) {
//        std::unique_lock<std::mutex> lock(mutex_);
//
//        not_empty_.wait(lock, [this] { return size_.load(std::memory_order_acquire) > 0; });
//
//        chunkPtr = std::move(buffer_[head_]);
//        buffer_[head_] = nullptr;
//
//        head_ = (head_ + 1) % Capacity;
//
//        size_t taskSize = chunkPtr->count.load() - chunkPtr->start.load();
//        size_.fetch_sub(taskSize, std::memory_order_release);
//
//        lock.unlock();
//        not_full_.notify_one();
//        return true;
//    }
//
//private:
//    ChunkAllocator* allocator;
//    //std::array<ChunkHandle, Capacity> buffer_{};
//    std::unique_ptr<Storage[]> buffer_;
//
//    size_t head_ = 0, tail_ = 0;
//    //全体のタスク数
//    std::atomic<size_t> size_{ 0 };
//    std::atomic<size_t> chunkSize_{ 0 };
//
//    std::mutex mutex_;
//    std::condition_variable not_empty_;
//    std::condition_variable not_full_;
//};


// in-place 構築用の大規模バッファ
class TaskArena {
    using TaskPtr = intrusive_ptr<Task>;

    std::vector<std::optional<TaskPtr>> buffer_;
    std::atomic<size_t> writePos_{ 0 };

public:
    TaskArena(size_t maxTasks){
        // 必ず MaxTasks の slot を用意しておく
        buffer_.resize(maxTasks);
    }

    // チャンク単位（ここでは 1～N）の先頭インデックスを確保
    size_t reserve(size_t count) {
        return writePos_.fetch_add(count, std::memory_order_relaxed);
    }

    template<typename... Args>
    TaskPtr constructAt(size_t idx, Args&&... args) {
        buffer_[idx].emplace(std::forward<Args>(args)...);
        return *buffer_[idx];
    }

    TaskPtr push(size_t idx,TaskPtr&& task) {
        buffer_[idx] = std::move(task);
        return *buffer_[idx];
    }

    //破棄
    void destroyAt(size_t idx) {
        buffer_[idx].reset(); 
    }

    TaskPtr* tryGetPtr(size_t idx) noexcept {
        return (buffer_[idx])
            ? std::addressof(*buffer_[idx])
            : nullptr;
    }

    /*std::optional<TaskPtr&> tryGet(size_t idx) noexcept {
        auto& slot = buffer_[idx];

        if (!slot.has_value()) return std::nullopt;

        return *slot;  
    }

    std::optional<const TaskPtr&> tryGet(size_t idx) const noexcept {
        auto& slot = buffer_[idx];

        if (!slot.has_value()) return std::nullopt;

        return *slot;
    }*/
};

struct SliceChunk {
    size_t start;
    size_t count;
    TaskArena* owner;
};

template<typename T, size_t MaxTasks,size_t MaxSliceSize>
class TaskStorage {
    using TaskArena = TaskArena;
    std::unique_ptr<TaskArena> arena;
    // チャンク記録用の軽量 MPMC キュー等を用意しておく
    std::deque<SliceChunk> sliceDeque;

public:
    TaskStorage(){
        arena = std::make_unique<TaskArena>(MaxTasks);
    }

    /*void pushMany(std::vector<Job>& jobs) {
        auto count = jobs.size();
        size_t start = arena.reserveChunk(count);

        for (size_t i = 0; i < count; ++i) {
            new (arena.ptr(start + i))
                T(std::move(jobs[i]));
        }

        push(count);
    }*/

    void enqueue(SliceChunk&& slice){
        if(sliceDeque.back().count == 0){
            sliceDeque.back() = std::move(slice);
            return;
        }

        sliceDeque.push_back(std::move(slice));
    }

    T pushOne(Job&& job,int degree,JobCategory cat) {
        // (A) バッファを 1 要素分確保
        size_t start = arena->reserve(1);

        auto result = arena->constructAt(start, new Task(std::move(job), degree, cat));

        push(start,1);

        return result;
    }

    T pushOne(T&&task) {
        // (A) バッファを 1 要素分確保
        size_t start = arena->reserve(1);

        auto result = arena->push(start,std::move(task));

        push(start, 1);

        return result;
    }

    SliceChunk popOne(){
        if(empty()){
            return {0,0};
        }

        return sliceDeque.pop_front();
    }

    std::vector<SliceChunk> popMany(size_t maxCount) {
        ASSERT(maxCount > 0,"Slice popMany() maxCount under zero");

        std::vector<SliceChunk> out;

        size_t curTaskCount = 0;
        //フルスライスを取り出す
        while(sliceDeque.size() > 1) {
            auto& c = sliceDeque.front();

            //取得するタスク数を超えた場合
            if(curTaskCount + c.count > maxCount){
                break;
            }

            curTaskCount += c.count;
            out.push_back(c);
            sliceDeque.pop_front();
        }

        // フルスライスが１つも無ければ、バックを返す
        if (out.empty() && !sliceDeque.empty() && sliceDeque.back().count > 0) {
            ASSERT(sliceDeque.back().count > maxCount,"back() size over");

            out.push_back(sliceDeque.back());
            sliceDeque.pop_back();
        }

        return out;
    }

    bool empty(){
        return sliceDeque.empty()||sliceDeque.front().count == 0;
    }

private:
    void push(size_t start,size_t count){
        if(sliceDeque.empty()){
            sliceDeque.push_back({start,0});
        }

        size_t newSize = sliceDeque.back().count + count;

        //最大値を超えた場合
        if (newSize >= MaxSliceSize) {
            sliceDeque.back().count = MaxSliceSize;
            size_t newStart = sliceDeque.back().start + MaxSliceSize;
            sliceDeque.push_back({ newStart,newSize - MaxSliceSize ,arena.get()});
        }
        else {
            sliceDeque.back().count = newSize;
        }
    }
};
}