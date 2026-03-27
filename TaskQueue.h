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

//struct Chunk{
//    std::vector<Job>jobs;
//    //std::vector<IJobBase*>dependents;
//    std::vector<std::shared_ptr<Inner>>inners;
//
//    std::atomic<size_t>head;
//    std::atomic<size_t>tail;
//};

struct ChunkMeta {
    std::vector<Job> jobs;
    JobCategory jobCategory;

    size_t size(){
        return jobs.size();
    }

    bool isEmpty(){
        return size() == 0;
    }

    ChunkMeta() = default;

    ChunkMeta(size_t reserveCount){
        jobs.reserve(reserveCount);
    }

    // コピー禁止
    ChunkMeta(const ChunkMeta&) = delete;
    ChunkMeta& operator=(const ChunkMeta&) = delete;

    // move のみ許可
    ChunkMeta(ChunkMeta&& other) noexcept
        : jobs(std::move(other.jobs)),
        jobCategory(other.jobCategory)
    {}

    ChunkMeta& operator=(ChunkMeta&& other) noexcept {
        if (this != &other) {
            jobs = std::move(other.jobs);
            jobCategory = other.jobCategory;
        }
        return *this;
    }
};

class JobManager;

//Chunkはリストのブロック
class TaskArena {
    TaskArena() = delete;

public:
    TaskArena(JobCategory jobCat,size_t maxBatch) : jobCategory(jobCat), batchMaxSize(maxBatch){currentBatchChunk.jobCategory = jobCat;};

     void enqueue(ChunkMeta&&chunk){
        std::lock_guard<std::mutex> guard(lock);

        chunks.emplace_back(std::move(chunk));
     }

     //Batch処理用の関数
    void enqueue(Job&&job){
        currentBatchChunk.jobs.emplace_back(std::move(job));

        if (currentBatchChunk.size() >= batchMaxSize) {
            enqueue(std::move(currentBatchChunk));

            currentBatchChunk.jobCategory = jobCategory;
            currentBatchChunk.jobs.clear();
        }
    }

    //chunkが一つもない
    bool isEmptyChunks() const noexcept{
        return 
            chunks.size() == 0;
    }

    //一つchunkPop
    bool popOne(ChunkMeta& chunk) {
        std::lock_guard lk(lock);
        
        if(isEmptyChunks()){
            return false;
        }
        
        chunk = std::move(chunks.front());
        chunks.pop_front();

        return true;
    }

    //chunkをpop
    void popMany(size_t maxCount,std::vector<ChunkMeta>&out){

        //取得スロットが空かchunksが空
        if(maxCount == 0||emptyFlag) return;

        //現在の満タンのchunkがない場合、代わりに未完成のchunkを積む
        if (isEmptyChunks()) {
            return;
        }
    
        out.reserve(std::min(maxCount, chunks.size()));

        size_t budget = maxCount;
    
        {
            std::lock_guard<std::mutex> guard(lock);

            while (!isEmptyChunks() && budget > 0) {

                out.push_back(std::move(chunks.front()));
                chunks.pop_front();
                budget--;
            }
        }
    }

    bool getFlushBatchChunk(ChunkMeta&chunk){
        if(currentBatchChunk.isEmpty()) return false;

        chunk = std::move(currentBatchChunk);

        currentBatchChunk.jobCategory = jobCategory;
        currentBatchChunk.jobs.clear();
        return true;
    }

private:
    std::deque<ChunkMeta> chunks;

    std::mutex lock;

    JobCategory jobCategory;

    bool emptyFlag = false;

    //Batch処理用のchunk詰め変数
    ChunkMeta currentBatchChunk;
    size_t batchMaxSize;
};

}