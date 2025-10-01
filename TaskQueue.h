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
// 
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

using TaskPtr = intrusive_ptr<Task>;

struct RangeTask{
    std::vector<TaskPtr>tasks;
    size_t maxSize = 16;
    JobCategory cat;
    bool isFull(){return tasks.size() >= maxSize;}
    size_t count(){return tasks.size();}
};

class TaskArena;

struct ChunkMeta {
    size_t offset;   // data[offset .. offset + capacity)
    size_t size;     // 現在の有効要素数
    size_t capacity; // 予約済み容量
    TaskArena* owner;
    
    bool isFull() {
        return size >= capacity;
    }

    bool isEmpty(){
        return size == 0;
    }
};

//struct Slice {
//    size_t start = 0;
//    size_t size = 0;
//    TaskArena* owner = nullptr;
//    uint8_t currentWorkload = 0;
//};

struct Slot {
    size_t offset;              // data 内の開始位置
    size_t size;               // 実際に格納された JobHandle 数
    TaskCategory category;      // このスロットのカテゴリ
};

//大規模バッファ
class TaskArena {
    std::vector<JobHandle> data;
    std::vector<Slot>slots;
    std::deque<ChunkMeta> chunks;

    std::unordered_map<TaskCategory,std::vector<JobHandle>>currentSlots;
    ChunkMeta currentChunk;

    std::mutex lock;

    size_t chunkMaxSize;

    const uint8_t maxWorkloadOfOneSlot;

public:
    TaskArena(uint8_t maxSlotWorkCap,size_t maxTasks, size_t chunkSize) : maxWorkloadOfOneSlot(maxSlotWorkCap),chunkMaxSize(chunkSize){
        data.reserve(maxTasks);

        for (int i = 0; i < static_cast<int>(TaskCategory::Num); i++) {
            auto cat = static_cast<TaskCategory>(i);
            currentSlots[cat] = {};
            currentSlots[cat].reserve(maxWorkloadOfOneSlot / getWorkload(cat));
        }

        chunkInitialize(currentChunk);

        /*currentSlots[TaskCategory::Easy] = std::vector<JobHandle>();
        currentSlots[TaskCategory::Easy].reserve(maxWorkloadOfOneSlot/getWorkload(TaskCategory::Easy));

        currentSlots[TaskCategory::Normal] = std::vector<JobHandle>();
        currentSlots[TaskCategory::Normal].reserve(maxWorkloadOfOneSlot / getWorkload(TaskCategory::Normal));*/
    }

    TaskCategory getTaskCategory(size_t offset,size_t slotIndex) {
        return slots[offset + slotIndex].category;
    }

    auto* getJobsBeginInSlot(size_t chunkOffset, size_t slotIndex) {
        Slot& s = slots[chunkOffset + slotIndex];
        return data.data() + s.offset;
    }

    auto* getJobsEndInSlot(size_t chunkOffset, size_t slotIndex) {
        Slot& s = slots[chunkOffset + slotIndex];
        return data.data() + s.offset + s.size;
    }

    size_t getSizeInSlot(size_t chunkOffset, size_t slotIndex) {
        Slot& s = slots[chunkOffset + slotIndex];
        return s.size;
    }

    JobHandle& at(size_t offset,size_t slotIndex, size_t localIndex) {
        return data[slots[offset + slotIndex].offset + localIndex];
    }

    void flushIncomplete(){
        std::lock_guard<std::mutex> guard(lock);
        //現在の未完成スロットすべて積む
        for(auto&[cat,slot] : currentSlots){
            if(!slot.empty()){
                pushJobs(cat,std::move(slot));
                slot = {};
                slot.reserve(maxWorkloadOfOneSlot/getWorkload(cat));
            }
        }

        //未完成のchunkも空でなければ積む
        if(!currentChunk.isEmpty()){
            chunks.push_back(std::move(currentChunk));
            chunkInitialize(currentChunk);
        }
    }

    void enqueue(TaskCategory cat,JobHandle handle){

        uint8_t workload = getWorkload(cat);

        std::lock_guard<std::mutex> guard(lock);
        if(workload >= maxWorkloadOfOneSlot){
            pushJob(cat,std::move(handle));
            return;
        }

        currentSlots[cat].push_back(std::move(handle));

        //一つslotが最大値に達したら
        if(isFullSlot(cat)){
            pushJobs(cat, std::move(currentSlots[cat]));
           
            currentSlots[cat] = {};
            currentSlots[cat].reserve(maxWorkloadOfOneSlot/workload);
            return;
        }
    }

    void enqueue(ChunkMeta&& chunk) {
        std::lock_guard<std::mutex> guard(lock);

        chunks.push_back(std::move(chunk));
    }

    //chunkが一つもない
    bool isEmpty() const noexcept{
        return chunks.size() == 0;
    }

    //破棄
    void destroyAt(size_t idx) {
        //data[idx].reset(); 
    }

    //一つchunkPop
    bool popOne(ChunkMeta&meta) {
        std::lock_guard lk(lock);
        
        if(isEmpty()){
            return false;
        }
        
        meta = std::move(chunks.front());
        chunks.pop_front();
        return true;
    }

    //chunkをpop
    void popMany(size_t maxCount,std::vector<ChunkMeta>&out){

        //取得スロットが空
        if(maxCount <= 0)return;

        //現在の満タンのchunkがない場合、代わりに未完成のchunkを積む
        if (isEmpty()) {
            flushIncomplete();
        }
    
        out.reserve(std::min(maxCount, chunks.size()));

        size_t budget = maxCount;
    
        {
            std::lock_guard<std::mutex> guard(lock);

            while (!isEmpty() && budget > 0) {
                out.push_back(std::move(chunks.front()));
                budget --;
                chunks.pop_front();
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

        slots.push_back({ offset, 1, cat });

        currentChunk.size++;

        //満杯なら現在のchunkを積む
        if (currentChunk.isFull()) {
            chunks.push_back(std::move(currentChunk));
            chunkInitialize(currentChunk);
        }
    }

    void pushJobs(TaskCategory cat, std::vector<JobHandle>&& jobs) {

        size_t size = jobs.size();

        // 新しい Slot を作成
        size_t offset = data.size();
        data.insert(data.end(),
            std::make_move_iterator(jobs.begin()),
            std::make_move_iterator(jobs.end()));

        slots.push_back({ offset, size, cat });

        currentChunk.size++;

        //満杯ならchunkを積む
        if (currentChunk.isFull()) {
            chunks.push_back(std::move(currentChunk));
            chunkInitialize(currentChunk);
        }
    }

    void chunkInitialize(ChunkMeta& chunk){
        chunk = {slots.size(), 0, chunkMaxSize,this };
    }
};

//template<typename T, size_t MaxTasks,size_t MaxSliceSize>
//class TaskStorage{
//    using TaskArena = TaskArena;
//
//public:
//    TaskStorage(){
//        arena = std::make_unique<TaskArena>(MaxSliceSize,MaxTasks);
//    }
//
//    /*void pushMany(std::vector<Job>& jobs) {
//        auto count = jobs.size();
//        size_t start = arena.reserveChunk(count);
//
//        for (size_t i = 0; i < count; ++i) {
//            new (arena.ptr(start + i))
//                T(std::move(jobs[i]));
//        }
//
//        push(count);
//    }*/
//
//    void enqueue(SliceChunk&& slice){
//        std::lock_guard<std::mutex> guard(lock);
//
//        if(sliceDeque.back().count == 0){
//            sliceDeque.back() = std::move(slice);
//            return;
//        }
//
//        sliceDeque.push_back(std::move(slice));
//    }
//
//    //void enqueue(Job&& job, int degree, JobCategory cat) override{}
//
//    T pushOrAppendRangeTask(Job&& job,JobCategory cat) {
//        if (!arena->isEmpty()) {
//            auto last = arena->lastPosition();
//            auto rangeTask = arena->tryGet(last);
//
//            if (rangeTask && !rangeTask->isFull()) {
//                return arena->constructTask(last, new Task(std::move(job), degree, cat));
//            }
//        }
//
//        // 新規RangeTask作成
//        size_t start = arena->reserve(1);
//        arena->constructAt(start);
//        arena->getRef(start).cat = cat;
//        auto result = arena->constructTask(start, new Task(std::move(job), degree, cat));
//        push(start, 1);
//        return result;
//    }
//
//    T pushJobHandle(JobHandle&& job, JobCategory cat, TaskCategory sizeCat = TaskCategory::Easy) {
//        if (!arena->isEmpty()) {
//            auto last = arena->lastPosition();
//            auto task = arena->tryGet(last);
//
//            if (rangeTask && !rangeTask->isFull()) {
//                return arena->constructTask(last, new Task(std::move(job), degree, cat));
//            }
//        }
//
//        // 新規RangeTask作成
//        size_t start = arena->reserve(1);
//        arena->constructAt(start);
//        arena->getRef(start).cat = cat;
//        auto result = arena->constructTask(start, new Task(std::move(job), degree, cat));
//        push(start, 1);
//        return result;
//    }
//
//    //T pushOne(Job&& job,int degree,JobCategory cat) {
//    //    // (A) バッファを 1 要素分確保
//    //    size_t start = arena->reserve(1);
//
//    //    auto result = arena->constructTask(start, new Task(std::move(job), degree, cat));
//
//    //    push(start,1);
//
//    //    return result;
//    //}
//
//    T pushOne(T&&task) {
//        ASSERT(false,"pushOne not work");
//        // (A) バッファを 1 要素分確保
//        size_t start = arena->reserve(1);
//
//        auto result = arena->push(start,std::move(task));
//
//        push(start, 1);
//
//        return result;
//    }
//
//    SliceChunk popOne(){
//        std::lock_guard lk(lock);
//
//        if(empty()){
//            return {0,0};
//        }
//
//        return sliceDeque.pop_front();
//    }
//
//    void popMany(size_t maxCount,std::vector<SliceChunk>&out){
//        ASSERT(maxCount > 0,"Slice popMany() maxCount under zero");
//
//        out.reserve((maxCount + MaxSliceSize - 1) / MaxSliceSize);
//
//        size_t budget = maxCount;
//
//        {
//            std::lock_guard<std::mutex> guard(lock);
//
//            if (empty()) {
//                return;
//            }
//
//            //フルスライスを取り出す
//            while (sliceDeque.size() > 1) {
//                auto& c = sliceDeque.front();
//                if (c.count > budget) break;
//                out.push_back(c);
//                budget -= c.count;
//                sliceDeque.pop_front();
//            }
//
//            // フルスライスが１つも無ければ、バックを返す
//            if (out.empty() && !sliceDeque.empty() && sliceDeque.back().count <= budget) {
//                ASSERT(sliceDeque.back().count <= budget, "back() size over");
//
//                out.push_back(sliceDeque.back());
//                sliceDeque.pop_back();
//            }
//        }
//    }
//
//    bool empty(){
//        return sliceDeque.empty()||sliceDeque.front().count == 0;
//    }
//
//private:
//    void push(size_t start,size_t count){
//        std::lock_guard<std::mutex> guard(lock);
//
//        //次にsliceを作るときのstart
//        size_t offset = start;
//        size_t remaining = count;
//
//        while (remaining > 0) {
//            //新規スライスが必要か、または末尾スライスが既に満杯なら新規作成
//            if (sliceDeque.empty() || sliceDeque.back().count == MaxSliceSize) {
//                sliceDeque.push_back({ offset, 0, arena.get() });
//            }
//
//            auto& curr = sliceDeque.back();
//            size_t space = MaxSliceSize - curr.count;
//            size_t toAdd = std::min(space, remaining);
//
//            curr.count += toAdd;
//            offset += toAdd;
//            remaining -= toAdd;
//        }
//    }
//
//private:
//    //全タスクを連続配置で保持
//    std::unique_ptr<TaskArena> arena;
//
//    // チャンク記録用の軽量キュー
//    std::deque<SliceChunk> sliceDeque;
//    std::mutex lock;
//};
}