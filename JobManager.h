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
#include <array>
#include "JobRecorder.h"
#include "JobDeque.hpp"
#include <utility>
#include "JobBarrier.h"
#include "JobDebugger.h"

namespace ECS::JobSystem{
   
    
    //using JobHandle = std::pair<TaskPtr,std::unique_ptr<IFuture>>;

//参考URL
// https://qiita.com/taqu/items/45ab4fb57e4079c3be94
// Qiita記事[Chunk allocator]
// 
//Capactityは512まで指定可能
template <typename T, size_t Capacity,size_t ChunkCount, size_t Align = 16>
class ChunkAllocator {
    using u16 = std::uint16_t;

public:
    struct Chunk {
        u16 next_{};
        std::atomic<u16> start{0};
        std::atomic<u16> count{0};
        std::array<T,Capacity> tasks{};
        bool full() const { return count.load(std::memory_order_acquire) == Capacity; }
        bool empty() const { return count.load(std::memory_order_acquire) == 0; }
    };
    
    static constexpr size_t EffectiveAlign = (Align > alignof(Chunk)) ? Align : alignof(Chunk);

    static constexpr size_t ChunkSize =
        (sizeof(Chunk) + EffectiveAlign - 1) & ~(EffectiveAlign - 1);

    static_assert(ChunkSize >= sizeof(Chunk), "ChunkSize must cover Chunk");
    static_assert((ChunkSize% EffectiveAlign) == 0, "ChunkSize must be multiple of EffectiveAlign");

    static constexpr size_t PageSize = ChunkCount * ChunkSize;
    static_assert(PageSize >= ChunkSize, "PageSize too small");

    static constexpr size_t ChunksPerPage = PageSize / ChunkSize;
    static_assert(ChunksPerPage > 0, "PageSize yields zero chunks");

    static constexpr u16 Invalid = std::numeric_limits<u16>::max();

    ChunkAllocator()
        : heap_(nullptr)
        , freeList_(Invalid)
    {
        static_assert(ChunksPerPage <= std::numeric_limits<u16>::max(), "Index overflow");

        heap_ = static_cast<std::byte*>(
            ::operator new(PageSize, std::align_val_t{ EffectiveAlign }));

        assert(heap_);

        for (u16 i = 0; i < ChunksPerPage; ++i) {
            void* p = heap_ + i * ChunkSize;
            ::new (p) Chunk();
            getChunk(i)->next_ = (i + 1 < ChunksPerPage) ? static_cast<u16>(i + 1) : Invalid;
        }
        freeList_ = 0;
    }

    ~ChunkAllocator() {
        for (size_t i = 0; i < ChunksPerPage; ++i) {
            getChunk(static_cast<u16>(i))->~Chunk();
        }

        ::operator delete(heap_, PageSize, std::align_val_t{ EffectiveAlign });
    }

    Chunk* allocate() {
        if (freeList_ == Invalid) {
            return nullptr; // 空きなし
        }
        const u16 index = freeList_;
        Chunk* chunk = getChunk(index);
        freeList_ = chunk->next_;

        chunk->start.store(0, std::memory_order_relaxed);
        chunk->count.store(0, std::memory_order_relaxed);

        return chunk;
    }

    void deallocate(Chunk* chunk) {
        if (!chunk) return;
        
        chunk->next_ = freeList_;
        chunk->start.store(0, std::memory_order_relaxed);
        chunk->count.store(0, std::memory_order_relaxed);

        const u16 index = getIndex(chunk);
        freeList_ = index;
    }

private:
    Chunk* getChunk(u16 idx) noexcept {
        return reinterpret_cast<Chunk*>(heap_ + idx * ChunkSize);
    }

    u16 getIndex(Chunk* chunk) {
        auto byteOffset = reinterpret_cast<std::byte*>(chunk) - reinterpret_cast<std::byte*>(heap_);
        return static_cast<u16>(byteOffset / ChunkSize);
    }

private:
    //Chunk* heap_;
    std::byte* heap_;
    u16 freeList_;
};

//T : 内部で保持する型
//Capacity : WaitJobBufferがためることができるChunkの最大数
//ChunkAllocator : Chunkを生成するクラス。
template <typename T, size_t Capacity,typename ChunkAllocator>
class WaitJobBuffer {
    using Chunk = typename ChunkAllocator::Chunk;

    struct ChunkDeleter {
        ChunkAllocator* alloc{};
        void operator()(Chunk* ptr) const noexcept {
            if (ptr && alloc) {
                alloc->deallocate(ptr);
            }
        }
    };

public:
    using ChunkPtr = std::unique_ptr<Chunk, ChunkDeleter>;

    WaitJobBuffer(ChunkAllocator* alloc): allocator(alloc){}

    // マルチスレッド
    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });

        if (buffer_[tail_] && buffer_[tail_]->full()) {
            tail_ = (tail_ + 1) % Capacity;
            size_.fetch_add(1, std::memory_order_release);
        }

        // 必要ならChunkを割り当て
        if (buffer_[tail_] == nullptr) {
            buffer_[tail_] = ChunkPtr(allocator->allocate(), ChunkDeleter{ allocator });
            buffer_[tail_]->count = 0;
        }

        auto& chunk = buffer_[tail_];
        chunk->tasks[chunk->count] = std::move(value);
        chunk->count.fetch_add(1,std::memory_order_release);

        lock.unlock();
        not_empty_.notify_one();
    }

    void push(ChunkPtr&& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity; });

        if (buffer_[tail_] && buffer_[tail_]->full()) {
            tail_ = (tail_ + 1) % Capacity;
            size_.fetch_add(1, std::memory_order_release);
        }

        if (buffer_[tail_] == nullptr) {
            //nullのスロットに直接代入
            buffer_[tail_] = std::move(value);
           
            lock.unlock();
            not_empty_.notify_one();
            return;
        }

        not_full_.wait(lock, [this] { return size_.load(std::memory_order_acquire) < Capacity-1; });
        //現在のスロットに引数のChunkを入れて、次のスロットに現在のChunkを入れる。
        size_.fetch_add(1, std::memory_order_release);
        size_t next = (tail_ + 1) % Capacity;
        buffer_[next] = std::move(value);
        std::swap(buffer_[tail_],buffer_[next]);
        tail_ = next;

        lock.unlock();
        not_empty_.notify_one();
    }

    bool try_pop(ChunkPtr& chunkPtr) {
        if (size_.load(std::memory_order_acquire) == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (size_.load(std::memory_order_acquire) == 0) {
            return false;
        }

        chunkPtr = std::move(buffer_[head_]);

        buffer_[head_] = nullptr;

        head_ = (head_ + 1) % Capacity;
        size_.fetch_sub(1, std::memory_order_release);

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

    bool wait_and_pop(ChunkPtr& chunkPtr) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] { return size_.load(std::memory_order_acquire) > 0; });

        chunkPtr = ChunkPtr(std::move(buffer_[head_]), ChunkDeleter{ allocator });
        buffer_[head_] = nullptr;

        head_ = (head_ + 1) % Capacity;
        size_.fetch_sub(1, std::memory_order_release);

        lock.unlock();
        not_full_.notify_one();
        return true;
    }

private:
    ChunkAllocator* allocator;
    std::array<ChunkPtr, Capacity> buffer_{};
    //std::array<T, Capacity> buffer_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    std::atomic<size_t> size_{ 0 };

    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

//旧待機キュー
//template<typename T>
//class WaitQueue {
//public:
//    void push(T v) {
//        std::lock_guard<std::mutex> lk(m);
//        q.push(std::move(v));
//        cv.notify_one();
//    }
//
//    bool try_pop(T& value){
//        std::lock_guard<std::mutex> lk(m);
//        if (q.empty()) {
//            return false;
//        }
//
//        value = std::move(q.front());
//        q.pop();
//        return true;
//    }
//
//private:
//    // 消費側は単一スレッドを想定
//    T pop() {
//        std::unique_lock<std::mutex> lk(m);
//        cv.wait(lk, [&] { return !q.empty(); });
//        T v = std::move(q.front());
//        q.pop();
//        return v;
//    }
//
//private:
//    std::queue<T> q;
//    std::mutex m;
//    std::condition_variable cv;
//};

class JobManager
{
    static constexpr size_t bufferCap = 20'000;

    //Chunkに詰められるTaskの最大数
    static constexpr size_t ChunkCap = 512;

    //1回の確保でまとめて作るchunkの数
    static constexpr size_t ChunkMemSize = 14;

    using ChunkAllocator = ChunkAllocator<TaskPtr, ChunkCap, ChunkMemSize,16>;

    using Chunk = ChunkAllocator::Chunk;

    //using WaitBuf = WaitQueue<TaskPtr>;
    using WaitBuf = WaitJobBuffer<TaskPtr,bufferCap,ChunkAllocator>;

    using ChunkPtr = WaitBuf::ChunkPtr;

    using JobQueue = Debug::DebugJobQueue<JobDeque<ChunkPtr>>;

    using StealResult = JobQueue::Base::StealResult;

    using PopResult = JobQueue::Base::PopResult;

    using PushResult = JobQueue::Base::PushResult;

    //仮として60FPS
    static constexpr float targetFPS = 60.0f;

    //1フレームあたりの時間
    static constexpr float frameTimeMs = 1000.0f / targetFPS;

    static constexpr double safetyMarginMs = 1.5;

    static constexpr double bgRatioMin = 0.10;
    static constexpr double bgRatioMax = 0.50;

    JobManager() = default;

    JobManager(JobManager&&) = delete;
    JobManager& operator=(JobManager&&) = delete;
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

public:
    static JobManager& Instance() {
        static JobManager manager;
        return manager;
    }

    void Initialize(size_t threadCount,
        std::unique_ptr<TimelineRecorder> rec = nullptr,
        size_t capacity = 1024);

    ~JobManager();

    //通常Job追加
    template<typename F>
    auto schedule_job(F&& func){

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {
                 if constexpr (std::is_void_v<R>) {
                    fn();           
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        if (t->inDegree.load() == 0) {
            pushRealTimeJobWaitQueue(t);
        }

        return std::make_pair(
            t,
            future
        );
    }

    //通常Job追加(依存Task)
    template<typename F>
    auto schedule_job(F&& func,const std::vector<TaskPtr>& deps) {

        using R = std::invoke_result_t<std::decay_t<F>>;

        auto [settable, future] = SettableJobFuture<R>::create();

        TaskPtr t{ new Task(
            Job([fn = std::forward<F>(func),
            setter = std::move(settable)]() mutable {

                if constexpr (std::is_void_v<R>) {
                    fn();            
                    setter.set_value(); //実行完了フラグを建てる
                }else {
                    setter.set_value(fn()); // 戻り値を取り出してセット
                }
            }),
            0
        ) };

        for (auto &d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d&&d->job.valid()) {
                addDependent(d.get(), t.get());
            }
        }

        if (t->inDegree.load() == 0) {
            pushRealTimeJobWaitQueue(t);
        }

        return std::make_pair(
            t, 
            future
        );
    }

    //debug付きJob追加
    template<typename F>
    auto schedule(char name,F&& func){

        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;

            fn();

            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped));
    }

    //debug付きJob追加
    template<typename F>
    auto schedule(char name, F&& func, const std::vector<TaskPtr>& deps){
    
        auto wrapped = [this,
            name,
            fn = std::forward<F>(func)]() mutable
        {
            int h = recorder ? recorder->recordStart(name) : 0;
            fn();
            if (recorder) recorder->recordEnd(h);
        };

        return schedule_job(std::move(wrapped), deps);
    }

    void waitForAll();

    void waitForAllRealTimeJob();

    void waitForLocalBackGroundJob();

    void workerThreadFunction(size_t queueIndex);

    bool isAbort() {
        return abortFlag.load(std::memory_order_acquire);
    }

    bool checkRanAllJobInJobQueues();

    IRecorder* getRecorder(){
        if(recorder){
            return recorder.get();
        }

        return nullptr;
    }

    //Taskが持つcategoryに応じて対応のwaitQueueにpushする
    void pushJobWaitQueue(TaskPtr task);
    
    //まとめてBackGroundJobをpushする用
    void pushBackGroudGlobalQueue(std::vector<TaskPtr>&& tasks);

    //フレーム始めに計算した処理数のBGを各ワーカーごとのBGQueueに割り振る。
    //計算は以下パラメータを使用する。
    //パラメータ(目標FPSms時間、ワンフレームでどのくらいBGを処理するかの比率、1ジョブの平均実行ms時間)
    void popGlobalBackGroundQueue();

    const size_t getThreadSize() const{ return threadSize;}

    void setStartFrameTime();

    std::chrono::steady_clock::time_point getStartFrameTime() const;

private:
    void pushRealTimeJobWaitQueue(TaskPtr task);

    //BGJobをglobalBGQueueにセット
    void pushBackGroudGlobalQueue(TaskPtr task);

    bool pushBottom(ChunkPtr&& chunkPtr, std::unique_ptr<JobQueue>& localQueue, std::unique_ptr<WaitBuf>& waitQueue);

    void pushLocalQueue(std::unique_ptr<JobQueue>&localQueue, std::unique_ptr<WaitBuf>&waitQueue);

    //GlobalBGQueueが空ならNull
    std::optional<TaskPtr>try_popGlobalBackGroundQueue();

    void run_realTimeQueue(size_t queueIndex);

    void run_backGroundQueue(size_t queueIndex);

    bool pop_and_steal_Queue(size_t queueIndex,std::vector<std::unique_ptr<JobQueue>>&stealQueues,std::function<void()>sub_counterFunc);

    StealStatus stealQueues(size_t queueIndex, std::vector<std::unique_ptr<JobQueue>>& stealQueues){
        size_t n = stealQueues.size();

        StealResult result;
        for (size_t i = 1; i < n; ++i) {
            size_t idx = (queueIndex + i) % n;
            result = stealQueues[idx]->stealTop(queueIndex);

            if (result.first == StealStatus::Success) {
                runChunk(queueIndex, std::move(result.second));
                return StealStatus::Success;
            }
        }

        return StealStatus::Empty;
    }

    void fallbackWaitQueue(std::unique_ptr<WaitBuf>& waitQueue,ChunkPtr chunkPtr) {
       waitQueue->push(std::move(chunkPtr));
    }

    void runChunk(size_t queueIndex,ChunkPtr&& chunkPtr);

    void runJob(size_t queueIndex,TaskPtr&& task);

    void run_while_validQueue(size_t queueIndex);

    bool allQueuesEmpty() const;

    void addDependent(Task* parent, Task* child) {
        // child を親の先頭に差し込む
        child->nextDependent = parent->nextDependent;
        parent->nextDependent = child;
        child->inDegree.fetch_add(1);
    }

    void abort();

    size_t getNextQueueIndex();

    size_t calculatePOPBGJobs(double target_ms, double elapsed_ms,double avgJobTime);

    void sub_realTimeJob_counter();

    void sub_backGroundJob_counter();

private:
    double avg_JobTimeMs = 1.0f;
    double avg_ExecuteJobTime = 0.1;

    std::unique_ptr<ChunkAllocator>allocator;

    // 時刻
    std::chrono::steady_clock::time_point frameStart;

    size_t threadSize;
    bool initFlag;

    std::vector<TaskPtr>globalBackGroudQueue;
    std::mutex backGroundMutex;

    //バックグラウンドで少しづつ処理される
    //全ての待機キューを処理時に個数を決めて取り出す。
    //処理フレームを問わない。
    //std::vector<std::unique_ptr<WaitQueue<TaskPtr>>>backGroundWaitQueues;
    std::vector<std::unique_ptr<WaitBuf>>backGroundWaitQueues;
    std::vector<std::unique_ptr<JobQueue>> backGroundLocalQueue;

    //リアルタイムキュー
    //優先的に処理される
    //1フレーム以内に処理を保証
    std::vector<std::unique_ptr<WaitBuf>> realTimeWaitQueues;
    std::vector<std::unique_ptr<JobQueue>> realTimeLocalQueue;

    std::vector<std::thread> workers;

    std::atomic<bool> stopFlag;
    
    //現ジョブの総数
    std::atomic<size_t> outstanding{ 0 };

    std::atomic<size_t> realTimeJobCounter;
    std::atomic<size_t> backGroundCounter;

    std::mutex stealMutex;

    std::mutex            wakeMutex;
    std::condition_variable wakeCv;

    std::mutex            realTimeJob_Mutex;
    std::condition_variable realTimeJob_WaitCv;

    std::mutex            backGroundJob_Mutex;
    std::condition_variable backGroundJob_WaitCv;

    std::atomic<size_t>      nextQueue{ 0 };
    std::condition_variable condition;
    std::mutex        finishMutex;
    std::condition_variable finishCv;

    std::unique_ptr<TimelineRecorder> recorder;

    std::atomic<bool> abortFlag{ false };
};

} //namespace ECS::JobSystem