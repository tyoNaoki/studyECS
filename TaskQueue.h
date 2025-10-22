#pragma once
#include "taskPtr.hpp"
#include <deque>

namespace ECS::JobSystem{

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

struct ChunkMeta {
    JobHandle* begin = nullptr;
    JobHandle* end = nullptr;
    std::atomic<size_t> slotNum = 0;
    std::vector<ChunkMeta*> dependents;
    std::atomic<uint32_t>inDegree = 0;

    size_t size() const {return end - begin;}

    bool isEmpty(){
        return slotNum.load(std::memory_order_relaxed) == 0;
    }

    JobCategory getJobCategory() const {return jobCategory;}

    ChunkMeta() = default; //デフォルト
    ChunkMeta(JobCategory category) : jobCategory(category) {}

    ChunkMeta(const ChunkMeta&) = delete;            // コピー禁止
    ChunkMeta& operator=(const ChunkMeta&) = delete; // コピー禁止

    ChunkMeta(ChunkMeta&& other) noexcept
        : begin(other.begin),
        end(other.end),
        dependents(std::move(other.dependents)),
        jobCategory(other.getJobCategory()),
        inDegree(other.inDegree.load(std::memory_order_relaxed))
    {
        other.begin = nullptr;
        other.end = nullptr;
        other.inDegree.store(0, std::memory_order_relaxed);
        slotNum.store(other.slotNum.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    ChunkMeta& operator=(ChunkMeta&& other) noexcept {
        if (this != &other) {
            begin = other.begin;
            end = other.end;
            dependents = std::move(other.dependents);
            jobCategory = other.getJobCategory();
            slotNum.store(other.slotNum.load(std::memory_order_relaxed), std::memory_order_relaxed);

            inDegree.store(other.inDegree.load(std::memory_order_relaxed),
                std::memory_order_relaxed);

            other.begin = nullptr;
            other.end = nullptr;
            other.inDegree.store(0, std::memory_order_relaxed);
        }
        return *this;
    }

    private:
        JobCategory jobCategory;
};

//Chunkはリストのブロック
class TaskArena {
    std::vector<JobHandle> data;
    std::deque<ChunkMeta> chunks;

    std::unordered_map<TaskCategory,std::vector<JobHandle>>currentSlots;
    ChunkMeta currentChunk;
    std::atomic<size_t>position;

    std::mutex lock;

    size_t chunkMaxSize;

    const uint8_t maxWorkloadOfOneSlot;
    size_t maxTaskCapacity = 0;

    const JobCategory jobCategory;

public:
    TaskArena(JobCategory category,uint8_t maxSlotWorkCap,size_t maxTasks, size_t chunkSize);

    void flushIncomplete();

    void enqueue(TaskCategory cat,JobHandle handle);

    void enqueue(ChunkMeta&& chunk) {
        std::lock_guard<std::mutex> guard(lock);

        chunks.push_back(std::move(chunk));
    }

    //chunkが一つもない
    bool isEmptyChunks() const noexcept{
        return chunks.size() == 0||chunks.size() == position.load(std::memory_order_relaxed);
    }

    bool clearAllJobHandles(){
        flushIncomplete();

        {
            std::lock_guard<std::mutex> guard(lock);

            //flushしても空なら、全て実行済みなので消去
            if(isEmptyChunks()){
                chunks.clear();
                data.clear();
                return true;
            }

            return false;
        }
    }

    //一つchunkPop
    bool popOne(ChunkMeta* chunk) {
        std::lock_guard lk(lock);
        
        if(isEmptyChunks()){
            return false;
        }
        
        size_t popPosition = position.fetch_add(1, std::memory_order_relaxed);
        chunk = &chunks[popPosition];

        return true;
    }

    //chunkをpop
    void popMany(size_t maxCount,std::vector<ChunkMeta*>&out){

        //取得スロットが空
        if(maxCount <= 0) return;

        //現在の満タンのchunkがない場合、代わりに未完成のchunkを積む
        if (isEmptyChunks()) {
            flushIncomplete();
        }
    
        out.reserve(std::min(maxCount, chunks.size()));

        size_t budget = maxCount;
    
        {
            std::lock_guard<std::mutex> guard(lock);

            while (!isEmptyChunks() && budget > 0) {
                size_t popPosition = position.fetch_add(1, std::memory_order_relaxed);
                out.push_back(&chunks[popPosition]);
                budget --;
            }
        }
    }

private:
    bool isFullSlot(TaskCategory cat){
        uint8_t workload = getWorkload(cat);
        return (workload * static_cast<uint8_t>(currentSlots[cat].size())) >= maxWorkloadOfOneSlot;
    }

    void pushJob(TaskCategory cat, JobHandle&& job) {
        size_t offset = data.size();
        data.push_back(std::move(job));

        if (!currentChunk.begin) {
            currentChunk.begin = &data.back();   // ← data 内の先頭を記録
        }

        currentChunk.slotNum++;

        if (isFullCurrentChunk()) {
            pushChunk(std::move(currentChunk));

            currentChunk = ChunkMeta(jobCategory);
        }
    }

    void pushJobs(TaskCategory cat, std::vector<JobHandle>&& jobs) {
        auto it = data.insert(data.end(),
            std::make_move_iterator(jobs.begin()),
            std::make_move_iterator(jobs.end()));

        if (!currentChunk.begin) {
            currentChunk.begin = &*it;   // ← data 内の先頭を記録
        }

        currentChunk.slotNum++;

        if (isFullCurrentChunk()) {
            pushChunk(std::move(currentChunk));

            currentChunk = ChunkMeta(jobCategory);
        }
    }

    bool isFullCurrentChunk(){
        return currentChunk.slotNum >= chunkMaxSize;
    }

    void pushChunk(ChunkMeta&&chunk) {
        chunk.end = data.data() + data.size();
        chunks.push_back(std::move(chunk));
    }

};

}