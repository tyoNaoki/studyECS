#pragma once
#undef min

#include <utility> 
#include <algorithm>
#include <cstddef>   // std::nullptr_t
#include <variant>
#include <memory>
#include <array>
#include "intrusive_ptr.h"
#include "TestFramework.hpp"
#include "HashFunctions.hpp"
#include "CallbackList.hpp"
#include <optional>

namespace ECS::JobSystem{
    
    using JobIndex = uint32_t;
    using JobVersion = uint32_t;
    using JobId = uint64_t;

    inline JobId composeJobId(JobIndex index, JobVersion version) {
        return (static_cast<JobId>(index) << 32) | static_cast<JobId>(version);
    }

    inline JobIndex getJobIndex(JobId id) {
        return id >> 32;//上位32ビットを取得
    }

    inline std::string jobInfo(JobId id) {
        return "[JOBID : '" + std::to_string(getJobIndex(id)) + "]";
    }

    inline JobVersion getJobVersion(JobId id) {
        return static_cast<JobVersion>(id & 0xFFFFFFFF); // 下位32ビットを取得
    }

    inline bool isEntityValid(JobId id) {
        return getJobIndex(id) != 0xFFFFFFFF; // 最大値と比較する
    }

    inline JobIndex NULL_JOB_INDEX = 0xFFFFFFFFu;
    inline JobId NULL_JOB_ID = composeJobId(0xFFFFFFFFu, 0u);

    //TaskCategoryをparallelJobに変更
    enum class TaskCategory : uint8_t {
        Normal = 0,
        Batch = 1,
        Num = 2,
    };

enum class JobCategory { RealTime, BackGround, Num };

#define REFLECT_FIELDS(Type, ...)                                  \
  static constexpr auto field_ptrs()                                \
  { return std::make_tuple(__VA_ARGS__); }

struct Job {

private:
    // 最大キャプチャ領域
    static constexpr size_t BufferSize = 32;

    // 呼び出し時の関数ポインタ型
    using Invoker = void(*)(void*,const size_t);
    using Destroyer = void(*)(void*);

    // 実データ格納＋呼び出し子
    alignas(void*) char  buf[BufferSize];
    Invoker invoke_fn = nullptr;
    Destroyer  destroy_fn = nullptr;

public:
    Job() = default;

    Job(Job&& o) noexcept {
        invoke_fn = o.invoke_fn;
        destroy_fn = o.destroy_fn;
        memcpy(buf, o.buf, BufferSize);
        o.invoke_fn = nullptr;
        o.destroy_fn = nullptr;
    }

    // 任意の小さいラムダ／関数オブジェクトをムーブキャプチャ
    template<typename F>
    Job(F&& f) noexcept {
        static_assert(sizeof(F) <= BufferSize,
            "Job function over BufferSize");
        new (buf) F(std::move(f));
        invoke_fn = [](void* p,const size_t id) {
            auto fp = static_cast<F*>(p);
            (*fp)(id);
        };

        destroy_fn = [](void* p) {
            static_cast<F*>(p)->~F();
        };
    }

    //デストラクター
    ~Job() {
        if (destroy_fn) {
            destroy_fn(buf);
        }
    }

    //ここで必要になるのがムーブ代入演算子
    Job& operator=(Job&& o) noexcept {
        if (this != &o) {
            // 破棄
            if (destroy_fn) destroy_fn(buf);

            //move
            invoke_fn = o.invoke_fn;
            destroy_fn = o.destroy_fn;
            std::memcpy(buf, o.buf, BufferSize);

            //クリア
            o.invoke_fn = nullptr;
            o.destroy_fn = nullptr;
        }
        return *this;
    }

    // 一度きりの実行
    void invoke(const size_t id) noexcept {
        if (invoke_fn) {
            invoke_fn(buf,id);
            if (destroy_fn) {
                destroy_fn(buf);
                destroy_fn = nullptr;
            }

            invoke_fn = nullptr;
        }
    }

    bool valid() const noexcept {
        return invoke_fn != nullptr;
    }

    // 暗黙の bool 変換は禁止
    explicit operator bool() const noexcept = delete;
};

struct FutureInner {
    FutureInner() = default;

    // コピー禁止
    FutureInner(const FutureInner&) = delete;
    FutureInner& operator=(const FutureInner&) = delete;

    // ムーブ禁止
    FutureInner(FutureInner&&) = delete;
    FutureInner& operator=(FutureInner&&) = delete;

    std::atomic<bool> ready{ false };
};

class JobFuture {
    std::shared_ptr<FutureInner> inner;
public:
    explicit JobFuture(std::shared_ptr<FutureInner> i)
        : inner(std::move(i)) {}

    //JobSystem::run_one_pending_job() を呼びつつ待ち
    bool isReady() {
        return inner->ready.load(std::memory_order_acquire);
    }
};

class SettableJobFuture {
    std::shared_ptr<FutureInner> inner;
public:
    explicit SettableJobFuture(std::shared_ptr<FutureInner> i)
        : inner(std::move(i)) {}

    static auto create() {
        auto ptr = std::make_shared<FutureInner>();
        return std::make_pair(
            SettableJobFuture{ ptr },
            JobFuture{ptr}
        );
    }

    // 結果なしの通知だけ
    void set_value() {
        inner->ready.store(true, std::memory_order_release);;
    }
};

//
//struct Task {
//    Job job;
//    std::atomic<int>   inDegree{ 0 };
//    intrusive_ptr<Task> nextDependent;
//    std::mutex taskMutex;
//    JobCategory category;
//    std::atomic<uint32_t> refCount;
//
//    Task(Job jb, int degree,JobCategory category)
//        : refCount(0)
//        , job(std::move(jb))
//        , inDegree(degree)
//        , nextDependent(nullptr)
//        ,category(category)
//    {}
//
//    void add_ref() { refCount.fetch_add(1, std::memory_order_relaxed); }
//
//    void release() {
//        if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
//
//           delete this;
//        }
//    }
//
//    void addDependent(Task* parent) {
//        // 自身を親の先頭に差し込む
//        nextDependent = parent->nextDependent;
//        parent->nextDependent = this;
//        inDegree.fetch_add(1);
//    }
//};

//template<typename T>
//struct SettableParallelJobFuture {
//
//    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<T>> i, std::shared_ptr<std::atomic<size_t>> count)
//        : inner(std::move(i)),counter(count){}
//
//    static auto create(size_t batchCount) {
//        auto ptr = std::make_shared<FutureInner<T>>();
//        auto parallelCount = std::make_shared<std::atomic<size_t>>(batchCount);
//        return std::make_pair(
//            SettableParallelJobFuture<T>{ ptr,parallelCount },
//            ParallelJobFuture{ ptr }
//        );
//    }
//
//    // 結果なしの通知だけ
//    size_t sub_counter() const {
//        return counter->fetch_sub(1, std::memory_order_acq_rel);
//    }
//
//    void set_value(T v) const {
//        std::lock_guard lk(inner->mtx);
//        inner->result = std::move(v);
//        inner->ready = true;
//    }
//
//    void set_exception(std::exception_ptr e) {
//        std::lock_guard lk(inner->mtx);
//        inner->eptr = std::move(e);
//        inner->ready = true;
//    }
//
//private:
//    std::shared_ptr<FutureInner<T>> inner;
//    std::shared_ptr<std::atomic<size_t>> counter;
//};
//
//template<>
//struct SettableParallelJobFuture<void> {
//   
//    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<void>> i, std::shared_ptr<std::atomic<size_t>> count)
//        : inner(std::move(i)), counter(count) {}
//
//    static auto create(size_t batchCount) {
//        auto ptr = std::make_shared<FutureInner<void>>();
//        auto parallelCount = std::make_shared<std::atomic<size_t>>(batchCount);
//        return std::make_pair(
//            SettableParallelJobFuture<void>{ ptr, parallelCount },
//            ParallelJobFuture{ ptr }
//        );
//    }
//
//    // 結果なしの通知だけ
//    size_t sub_counter() const {
//        return counter->fetch_sub(1, std::memory_order_acq_rel);
//    }
//
//    void set_value() const {
//        std::lock_guard lk(inner->mtx);
//        inner->ready = true;
//    }
//
//    void set_exception(std::exception_ptr e) {
//        std::lock_guard lk(inner->mtx);
//        inner->eptr = std::move(e);
//        inner->ready = true;
//    }
//
//private:
//    std::shared_ptr<FutureInner<void>> inner;
//    std::shared_ptr<std::atomic<size_t>> counter;
//};
//
//template<typename S>
//struct JobConnector {
//    using Ptrs = decltype(S::field_ptrs());
//    static constexpr size_t N = std::tuple_size_v<Ptrs>;
//
//    Ptrs ptrs_{ S::field_ptrs() };
//
//    // I番目のメンバポインタを取り出し
//    template<size_t I>
//    constexpr auto get() const noexcept {
//        return std::get<I>(ptrs_);
//    }
//};

struct DummyBuffer {};



// 結果を持つ場合
template<typename T>
struct ResultHolder {
protected:
    T result;
};

template<typename T>
struct TaskFuture;

template<typename>
struct IJob;

class JobManager;

struct Inner {
    Inner() = default;

    // コピー禁止
    Inner(const Inner&) = delete;
    Inner& operator=(const Inner&) = delete;

    // ムーブ禁止
    Inner(Inner&&) = delete;
    Inner& operator=(Inner&&) = delete;

    void setReady(bool r) {
        ready.store(r, std::memory_order_release);
    }

    bool isReady() const {
        return ready.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> ready{ false };
};

struct JobHandle {
    bool isComplete() const;

    void Complete() const;

    JobId getJobId() { return jobId; }

    // 外部には見せない
    static JobHandle createHandle(JobId id, std::shared_ptr<Inner>&& inner) {
        JobHandle h;
        h.jobId = id;
        h.inner = std::move(inner);
        return h;
    }

private:
    JobId jobId;
    std::shared_ptr<Inner>inner;

    // デフォルトコンストラクタ
    JobHandle() = default;
};

struct IJobBase {
    virtual ~IJobBase() = default;
};

template<typename Derived>
struct IJob : public IJobBase,std::enable_shared_from_this<Derived>{

private:
    struct  Context
    {
        std::shared_ptr<Derived> self;
        std::shared_ptr<Inner>setter;
        JobId jobId;

        Context(std::shared_ptr<Derived> s,
        JobId Id,
        std::shared_ptr<Inner>set):self(s),jobId(Id),setter(set){}
    };

protected:
    IJob() = default;

public:
    virtual ~IJob(){
        //std::printf("JobID %zu deleted \n",getJobIndex(jobId));
    };

    template<typename... Args>
    static std::shared_ptr<Derived> create(Args&&... args){
        return std::make_shared<Derived>(std::forward<Args>(args)...);
    }

    void addRequest(std::function<void(Derived&)>&& fn) {
        commands.emplace_back(std::move(fn));
    }

    void addRequest(std::vector<std::function<void(Derived&)>>&& fns) {
        std::swap(commands,fns);
    }

    JobHandle scheduleIJob(TaskCategory taskCategory = TaskCategory::Normal, JobCategory jobCategory = JobCategory::RealTime) {

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();
        auto jobId = jm.emplaceJobID();
        auto setter = std::make_shared<Inner>();

        auto context = std::make_shared<Context>(self,jobId,setter);

        auto work = [context](const size_t workerId) { executeIJob(context->self.get(),context->jobId,context->setter.get(),workerId);};
        Job job(std::move(work));

        jm.scheduleJobHandle(taskCategory,jobCategory,jobId,std::move(job));
        
        auto handle = JobHandle::createHandle(jobId, std::move(setter));

        return handle;
    }

    JobHandle scheduleIJob(JobHandle& dependentHandle,TaskCategory taskCategory = TaskCategory::Normal, JobCategory jobCategory = JobCategory::RealTime) {

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();
        auto jobId = jm.emplaceJobID();
        auto setter = std::make_shared<Inner>();

        auto context = std::make_shared<Context>(self, jobId, setter);

        auto work = [context](const size_t workerId) { executeIJob(context->self.get(), context->jobId, context->setter.get(), workerId); };
        Job job(std::move(work));

        jm.scheduleJobHandle(taskCategory, jobCategory, jobId, std::move(job), dependentHandle);

        auto handle = JobHandle::createHandle(jobId, std::move(setter));

        return handle;
    }

    JobHandle scheduleIJob(std::vector<JobHandle>& dependentHandles, TaskCategory taskCategory = TaskCategory::Normal, JobCategory jobCategory = JobCategory::RealTime) {

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();
        auto jobId = jm.emplaceJobID();
        auto setter = std::make_shared<Inner>();

        auto context = std::make_shared<Context>(self, jobId, setter);

        auto work = [context](const size_t workerId) { executeIJob(context->self.get(), context->jobId, context->setter.get(), workerId); };
        Job job(std::move(work));

        jm.scheduleJobHandle(taskCategory, jobCategory, jobId, std::move(job), dependentHandles);

        auto handle = JobHandle::createHandle(jobId, std::move(setter));

        return handle;
    }

private:
    static void executeIJob(Derived* self,JobId jobId,Inner*setter,const size_t workerID) {
        self->execute();
        setter->setReady(true);

        auto& jm = JobManager::Instance();

        //依存解決
        jm.processDependents(workerID,jobId);

        //cleanUP用
        jm.completedJob(workerID,jobId);
    };

    void applyCommands() {

        if (commands.empty()) return;

        for (auto& fn : commands) {
            fn(static_cast<Derived&>(*this)); // 直接呼び出し
        }

        commands.clear();
    }

    std::shared_ptr<Derived> shared_this(){
        return std::enable_shared_from_this<Derived>::shared_from_this();
    }

protected:

    inline void execute() {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute() member function!!");
    }

    //friend class JobManager;

private:
    // コマンドバッファ
    std::vector<std::function<void(Derived&)>> commands;
};
//
//template<typename Derived,TaskCategory>
//struct IJob
//    : public std::enable_shared_from_this<Derived>
//    , ResultHolder<ReturnType> {
//    using Return_t = ReturnType;
//
//    using HasReturn = std::bool_constant<!std::is_same_v<Return_t, void>>;
//
//    struct Context {
//        std::shared_ptr<Derived> self;
//
//        Context(std::shared_ptr<Derived> s)
//            : self(s) {}
//    };
//
//public:
//    template<typename... Args>
//    static std::shared_ptr<Derived> Create(Args&&... args) {
//        // make_shared で Derived を生成
//        return std::make_shared<Derived>(std::forward<Args>(args)...);
//    }
//    
//    void AddRequeset(std::function<void(Derived&)>&& fn) {
//        commands.emplace_back(std::move(fn));
//    }
//
//    JobHandle scheduleIJob(){
//        auto* self = static_cast<Derived*>(this);
//
//        auto work = [self]() { ExecuteIJob(self); };
//        Job job(std::move(work));
//
//        auto& jm = JobManager::Instance();
//
//        return jm.scheduleJobHandle(TaskCategory::Easy, JobCategory::RealTime, std::move(job));
//    }
//
//    std::shared_ptr<Derived> shared_this()
//    {
//        return std::enable_shared_from_this<Derived>::shared_from_this();
//    }
//
//protected:
//    inline void Execute() {
//        // Derived の Execute() を呼び出し
//        //static_cast<Derived*>(this)->Execute(index);
//        //ASSERT(false, "Derived class not found Execute() member function!!");
//    }
//
//    
//
//private:
//    IJob() = default;
//
//    static void ExecuteIJob(Derived* self) {
//        ASSERT(self, "self is nullptr");
//
//        self->Execute();
//    };
//
//    static void ExecuteIJob(std::shared_ptr<Context> ctx) {
//        ASSERT(ctx->self, "self is nullptr");
//    
//        ctx->self->Execute();
//        //ctx->self->setInner();
//    };
//
//    void applyCommands() {
//
//        if(commands.empty()) return;
//
//        for (auto& fn : commands) {
//            fn(static_cast<Derived&>(*this)); // 直接呼び出し
//        }
//
//        commands.clear();
//    }
//
//protected:
//    using ResultHolder<ReturnType>::result;
//
//    friend struct TaskFuture<Derived>;
//
//private:
//    // コマンドバッファ
//    std::vector<std::function<void(Derived&)>> commands;
//};

//parallelJobの基底クラス
//Derived : 派生クラス
//ParallelJoData : Job実行時に書き込むデータ
//template<typename Derived,typename ParallelJobData  = std::monostate>
//struct IParallelJob : std::enable_shared_from_this<Derived>{
//    using TaskPtr = intrusive_ptr<Task>;
//
//    using Data_t = ParallelJobData;
//
//    using HasData = std::bool_constant<!std::is_same_v<Data_t, std::monostate>>;
//
//    using Future_t = std::conditional_t<
//        HasData::value,
//        ParallelJobFuture<Data_t>,
//        ParallelJobFuture<void>
//        >;
//
//    using Setter_t = std::conditional_t<
//        HasData::value,
//        SettableParallelJobFuture<Data_t>,
//        SettableParallelJobFuture<void>
//        >;
//
//    IParallelJob() = default;
//
//    template <typename T = Data_t,
//        typename = std::enable_if_t<!std::is_same_v<T, void>>>
//        IParallelJob(T&& d)
//        : jobResult(std::forward<T>(d)) {}
//    
//    ~IParallelJob(){};
//
//    struct Context {
//        std::shared_ptr<Derived> self;
//        size_t total, batchSize, numBatches, chunkBatches;
//        std::atomic<size_t>* nextBatch;
//        Setter_t setter;
//
//        Context(std::atomic<size_t>* next)
//            : total(0), batchSize(0), numBatches(0), chunkBatches(0),nextBatch(next), setter(nullptr,0) {}
//
//         Context(std::shared_ptr<Derived> s,
//            size_t t, size_t b, size_t n,
//            size_t c,
//            std::atomic<size_t>* next,
//            Setter_t&& set)
//      : self(s),total(t),batchSize(b),numBatches(n), chunkBatches(c),nextBatch(next),setter(std::move(set)){}
//
//        ~Context(){};
//    };
//
//    inline std::pair<std::vector<TaskPtr>, Future_t> schedule(const size_t total,const size_t batchSize,const size_t workerCount, JobCategory cat = JobCategory::RealTime) {
//        ASSERT(total > 0 && batchSize > 0, "parallelJob schedule total or batchSize is zero");
//
//        const size_t numBatches = (total + batchSize - 1) / batchSize;
//
//        const size_t threadSize = JobManager::Instance().getThreadSize();
//        const size_t workerNum = std::min({threadSize,workerCount,numBatches});
//
//        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));
//
//        auto [settable, future] = Setter_t::create(numBatches);
//
//        nextBatch_.store(0, std::memory_order_relaxed);
//
//        auto ctx = std::make_shared<Context>(
//            shared_this()
//            , total
//            , batchSize
//            , numBatches
//            , chunkBatches
//            , &nextBatch_
//            , std::move(settable)
//            );
//
//        std::vector<TaskPtr>tasks;
//        tasks.reserve(workerNum);
//
//        // ワーカー数分だけ Task を作成して登録
//        for (size_t w = 0; w < workerNum; ++w) {
//            auto work = [ctx]() { workerEntry(ctx); };
//
//            /*auto task = new Task(Job(std::move(work)), 0,cat);
//            TaskPtr taskPtr{ std::move(task) };*/
//
//            Job job(std::move(work));
//
//            auto taskPtr = JobManager::Instance().scheduleTask(cat, std::move(job), 0);
//
//            //JobManager::Instance().pushJobWaitQueue(taskPtr);
//
//            tasks.push_back(taskPtr);
//        }
//
//        return std::make_pair(tasks, future);
//    }
//
//    inline std::pair<std::vector<TaskPtr>,Future_t> schedule(JobCategory cat,size_t total,size_t batchSize,size_t workerCount,const std::vector<TaskPtr>&deps){
//        ASSERT(total > 0 && batchSize > 0&& workerCount > 0,"parallelJob schedule total or batchSize is zero");
//
//        const size_t numBatches = (total + batchSize - 1) / batchSize;
//        const size_t threadSize = JobManager::Instance().getThreadSize();
//        const size_t workerNum = std::min({ threadSize,workerCount,numBatches });
//
//        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));
//
//        auto [settable, future] = Setter_t::create(numBatches);
//        
//        nextBatch_.store(0, std::memory_order_relaxed);
//
//        auto ctx = std::make_shared<Context>(
//            shared_this()
//            ,total
//            ,batchSize
//            ,numBatches
//            ,chunkBatches
//            ,&nextBatch_
//            ,std::move(settable)
//            );
//
//        std::vector<TaskPtr>tasks;
//        tasks.reserve(workerNum);
//
//        // ワーカー数分だけ Task を作成して登録
//        for (size_t w = 0; w < workerCount; ++w) {
//            auto work = [ctx]() { workerEntry(ctx); };
//
//            auto task = new Task(Job(std::move(work)), 0,cat);
//            TaskPtr taskPtr{std::move(task)};
//
//            for (auto& d : deps) {
//                std::lock_guard<std::mutex> lk(d->taskMutex);
//                if (d && d->job.valid()) {
//                    taskPtr.get()->addDependent(d.get());
//                }
//            }
//
//            if (taskPtr->inDegree.load() == 0) {
//                JobManager::Instance().scheduleTask(taskPtr);
//            }
//
//            tasks.push_back(taskPtr);
//        }
//
//        return std::make_pair(tasks,future);
//    }
//
//    void AddRequeset(std::function<void(Derived&)>&& fn) {
//        commands.emplace_back(std::move(fn));
//    }
//   
//private:
//    static void workerEntry(std::shared_ptr<Context> ctx) {
//        auto self = ctx->self;
//        if(!self){
//            std::printf("self is nullptr");
//            return;
//        }
//        const size_t batchSize = ctx->batchSize;
//        const size_t numBatches = ctx->numBatches;
//        const size_t total = ctx->total;
//        const size_t chunkBatches = ctx->chunkBatches;
//
//        while (true) {
//            //回数見直し、バッチ回数に合わせる。応じて変える
//            //numBatchesも
//            const size_t idx = ctx->nextBatch->fetch_add(chunkBatches, std::memory_order_relaxed);
//
//            if(idx >= numBatches) return;
//
//            const size_t taken = std::min(chunkBatches, numBatches - idx);
//
//            for (size_t b = 0; b < taken; ++b) {
//                const size_t start = (idx + b) * batchSize;
//
//                ASSERT(start < total,"IParallelJob workerEntry : start >= total");
//
//                const size_t len = std::min(batchSize, total - start);
//
//                self->ExecuteBatch(start,len,self.get());
//
//                if (ctx->setter.sub_counter() == 1) {
//                    if constexpr (HasData::value){
//                        ctx->setter.set_value(self->jobResult);
//                    }else{
//                        ctx->setter.set_value();
//                    }
//                    
//                    return;
//                }
//            }
//        }
//    }
//
//    inline void Execute(size_t index) {
//        // Derived の Execute() を呼び出し
//        //static_cast<Derived*>(this)->Execute(index);
//        ASSERT(false, "Derived class not found Execute(size_t) member function!!");
//    }
//
//    void applyCommands() {
//
//        if (commands.empty()) return;
//
//        for (auto& fn : commands) {
//            fn(static_cast<Derived&>(*this)); // 直接呼び出し
//        }
//
//        commands.clear();
//    }
//
//protected:
//    inline void ExecuteBatch(const size_t start,const size_t len,Derived*self) {
//        for (size_t i = start; i < start + len; ++i) {
//            self->Execute(i);
//        }
//    }
//
//    std::shared_ptr<Derived> shared_this()
//    {
//        return std::enable_shared_from_this<Derived>::shared_from_this();
//    }
//
//    [[no_unique_address]] Data_t jobResult;
//
//private:
//    std::atomic<size_t> nextBatch_{ 0 };
//
//    // コマンドバッファ
//    std::vector<std::function<void(Derived&)>> commands;
//
//    size_t threadSize;
//};

}//namespace ECS::JobSystem
