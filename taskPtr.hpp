#pragma once
#undef min

#include <utility> 
#include <algorithm>
#include <cstddef>   // std::nullptr_t
#include "JobManager.h"
#include <variant>
#include <memory>
#include "TestFramework.hpp"

namespace ECS::JobSystem{

#define REFLECT_FIELDS(Type, ...)                                  \
  static constexpr auto field_ptrs()                                \
  { return std::make_tuple(__VA_ARGS__); }

namespace Ptr{

template<typename T>
class intrusive_ptr {
public:
    // -- constructors/destructor --
    intrusive_ptr() noexcept
        : ptr_(nullptr) {}

    intrusive_ptr(std::nullptr_t) noexcept
        : ptr_(nullptr) {}

    // raw ポインタから参照カウントを +1 して保持
    explicit intrusive_ptr(T* p) noexcept
        : ptr_(p) {
        //if (ptr_) intrusive_ptr_add_ref(ptr_);
        if (ptr_) ptr_->add_ref();
    }

    // コピー：参照カウンタ +1
    intrusive_ptr(intrusive_ptr const& o) noexcept
        : ptr_(o.ptr_) {
        //if (ptr_) intrusive_ptr_add_ref(ptr_);
        if (ptr_) ptr_->add_ref();
    }

    // ムーブ：コピー先だけ ptr を奪う（参照カウントは変えない）
    intrusive_ptr(intrusive_ptr&& o) noexcept
        : ptr_(o.ptr_) {
        o.ptr_ = nullptr;
    }

    // デストラクタ：参照カウント -1、0 なら delete
    ~intrusive_ptr() noexcept {
        //if (ptr_) intrusive_ptr_release(ptr_);
        if (ptr_) ptr_->release();
    }

    // -- assignment operators --
    intrusive_ptr& operator=(intrusive_ptr const& o) noexcept {
        intrusive_ptr tmp(o);
        swap(tmp);
        return *this;
    }

    intrusive_ptr& operator=(intrusive_ptr&& o) noexcept {
        intrusive_ptr tmp(std::move(o));
        swap(tmp);
        return *this;
    }

    intrusive_ptr& operator=(T* p) noexcept {
        intrusive_ptr tmp(p);
        swap(tmp);
        return *this;
    }

    // -- modifiers & observers --
    void swap(intrusive_ptr& o) noexcept {
        std::swap(ptr_, o.ptr_);
    }

    T* get() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // -- dereference --
    T& operator* () const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

private:
    T* ptr_;
   
};

// non-member swap
template<typename T>
inline void swap(intrusive_ptr<T>& a, intrusive_ptr<T>& b) noexcept {
    a.swap(b);
}

// 比較演算子
template<typename T, typename U>
bool operator==(intrusive_ptr<T> const& a, intrusive_ptr<U> const& b) noexcept {
    return a.get() == b.get();
}
template<typename T, typename U>
bool operator!=(intrusive_ptr<T> const& a, intrusive_ptr<U> const& b) noexcept {
    return a.get() != b.get();
}
template<typename T>
bool operator==(intrusive_ptr<T> const& a, std::nullptr_t) noexcept {
    return !a;
}
template<typename T>
bool operator!=(intrusive_ptr<T> const& a, std::nullptr_t) noexcept {
    return static_cast<bool>(a);
}

//inline void intrusive_ptr_add_ref(Task* p) { p->add_ref(); }
//inline void intrusive_ptr_release(Task* p) { p->release(); }

} //namespace Ptr

struct Job {

private:
    // 最大キャプチャ領域
    static constexpr size_t BufferSize = 32;

    // 呼び出し時の関数ポインタ型
    using Invoker = void(*)(void*);
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
        invoke_fn = [](void* p) {
            auto fp = static_cast<F*>(p);
            (*fp)();
        };

        destroy_fn = [](void* p) {
            static_cast<F*>(p)->~F();
        };

        if(destroy_fn){
            int a = 0;
        }
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
            // 1) 既存のキャプチャを破棄
            if (destroy_fn) destroy_fn(buf);

            // 2) データをムーブ
            invoke_fn = o.invoke_fn;
            destroy_fn = o.destroy_fn;
            std::memcpy(buf, o.buf, BufferSize);

            // 3) ムーブ元をクリア
            o.invoke_fn = nullptr;
            o.destroy_fn = nullptr;
        }
        return *this;
    }

    // 一度きりの実行
    void invoke() noexcept {
        if (invoke_fn) {
            invoke_fn(buf);
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

enum class JobCategory : uint8_t {
    RealTime,
    BackGround
};

struct Task {
    Job job;
    std::atomic<int>   inDegree{ 0 };
    Ptr::intrusive_ptr<Task> nextDependent;
    std::mutex taskMutex;
    JobCategory category;
    std::atomic<uint32_t> refCount;

    Task(Job jb, int degree,JobCategory category)
        : refCount(0)
        , job(std::move(jb))
        , inDegree(degree)
        , nextDependent(nullptr)
        ,category(category)
    {}

    void add_ref() { refCount.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void addDependent(Task* parent) {
        // 自身を親の先頭に差し込む
        nextDependent = parent->nextDependent;
        parent->nextDependent = this;
        inDegree.fetch_add(1);
    }
};



// 未 specialization：結果を持てる型用
template<typename T>
struct FutureInner {
    std::mutex       mtx;
    std::atomic<bool> ready{ false };
    std::optional<T> result;
    std::exception_ptr eptr;
};

// void 専用 specialization：result を持たない
template<>
struct FutureInner<void> {
    std::mutex       mtx;
    std::atomic<bool> ready{ false };
    std::exception_ptr eptr;
};

struct IFuture {
    virtual bool isReady() const = 0;
    virtual void wait() = 0;
};

// 待ち手（読み取り専用ハンドル）
template<typename T>
struct JobFuture : public IFuture {
    explicit JobFuture(std::shared_ptr<FutureInner<T>> i)
        : inner(std::move(i)) {}

    //JobSystem::run_one_pending_job() を呼びつつ待ち
    T wait_and_get() {
        while (true) {
            // まず mutex を獲得して ready フラグをチェック
            if (inner->ready) {
                static_assert(!std::is_void_v<T>, "JobFuture<T> is void");
                std::lock_guard lk(inner->mtx);

                if (inner->eptr)
                    std::rethrow_exception(inner->eptr);

                ASSERT(inner->result, "inner->result is nullptr!!");

                T value = std::move(*inner->result);

                // オリジナルのポインタをリセット
                *inner->result.reset();
                inner->ready.store(false, std::memory_order_relaxed);

                return value;
            }

            // まだ ready でなければ他ジョブをひとつ消化
            //jobSystem.();
        }
    }

    void wait() override {
        while (true) {
            {
                //std::lock_guard lk(inner->mtx);
                if (inner->ready) {
                    if (inner->eptr)
                        std::rethrow_exception(inner->eptr);

                    if constexpr (!std::is_void_v<T>) {
                        return;
                    }
                    else {
                        return;
                    }
                }
            }

            // まだ ready でなければ他ジョブをひとつ消化
            //jobSystem.run_one_pending_job();
        }
        return;
    }

    bool isReady() const override {
        return inner->ready;
    }

private:
    std::shared_ptr<FutureInner<T>> inner;
};

//voidバージョン
template<>
struct JobFuture<void> : public IFuture {
    explicit JobFuture(std::shared_ptr<FutureInner<void>> i)
        : inner(std::move(i)) {}

    void wait() override {
        while (true) {
            if (inner->ready) {
                if (inner->eptr)
                    std::rethrow_exception(inner->eptr);
                return;
            }
        }
    }

    bool isReady() const override {
        return inner->ready;
    }

private:
    std::shared_ptr<FutureInner<void>> inner;
};

template<typename T>
struct ParallelJobFuture : public IFuture {
    explicit ParallelJobFuture(std::shared_ptr<FutureInner<T>> i)
        : inner(std::move(i)) {}

    void wait() override {
        while (!isReady()) {}

        if (inner->eptr)
            std::rethrow_exception(inner->eptr);
        return;
    }

    //非void
    T wait_and_get() {
        while (true) {
            // まず mutex を獲得して ready フラグをチェック
            {
                if (isReady()) {
                    static_assert(!std::is_void_v<T>, "JobFuture<T> is void");

                    std::lock_guard lk(inner->mtx);
                    if (inner->eptr)
                        std::rethrow_exception(inner->eptr);

                    ASSERT(inner->result, "innter result is nullptr");

                    return std::move(*inner->result);
                }
            }

            // まだ ready でなければ他ジョブをひとつ消化
            //jobSystem.();
        }
    }

    //void
    template<typename U = T>
    std::enable_if_t< std::is_void_v<U>, void> 
    wait_and_get() {
        while (true) {
            {
                if (isReady()) {
                    if (inner->eptr)
                        std::rethrow_exception(inner->eptr);
                    return;
                }
            }

            // まだ ready でなければ他ジョブをひとつ消化
            //jobSystem.();
        }
    }

    bool isReady() const override {
        return inner->ready;
    }

private:
    std::shared_ptr<FutureInner<T>> inner;
};

// 書き込み手（セット専用ハンドル）
template<typename T>
struct SettableJobFuture {
    explicit SettableJobFuture(std::shared_ptr<FutureInner<T>> i)
        : inner(std::move(i)) {}

    // Futureペアを作って返すユーティリティ
    static auto create() {
        auto ptr = std::make_shared<FutureInner<T>>();
        return std::make_pair(
            SettableJobFuture{ ptr },
            JobFuture<T>{ptr}
        );
    }

    // 実行タスク側が結果をセットする
    void set_value(T v) {
        std::lock_guard lk(inner->mtx);
        inner->result = std::move(v);
        inner->ready = true;
    }

    void set_exception(std::exception_ptr e) {
        std::lock_guard lk(inner->mtx);
        inner->eptr = std::move(e);
        inner->ready = true;
    }

private:
    std::shared_ptr<FutureInner<T>> inner;
};

// void 専用 write-only specialization
template<>
class SettableJobFuture<void> {
    std::shared_ptr<FutureInner<void>> inner;
public:
    explicit SettableJobFuture(std::shared_ptr<FutureInner<void>> i)
        : inner(std::move(i)) {}

    static auto create() {
        auto ptr = std::make_shared<FutureInner<void>>();
        return std::make_pair(
            SettableJobFuture{ ptr },
            JobFuture<void>{ptr}
        );
    }

    // 結果なしの通知だけ
    void set_value() {

        std::lock_guard lk(inner->mtx);
        inner->ready = true;
    }

    void set_exception(std::exception_ptr e) {
        std::lock_guard lk(inner->mtx);
        inner->eptr = std::move(e);
        inner->ready = true;
    }
};

template<typename T>
class SettableParallelJobFuture {
    std::shared_ptr<FutureInner<T>> inner;
    std::shared_ptr<std::atomic<size_t>> counter;
public:
    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<T>> i, std::shared_ptr<std::atomic<size_t>> count)
        : inner(std::move(i)),counter(count){}

    static auto create(size_t batchCount) {
        auto ptr = std::make_shared<FutureInner<T>>();
        auto parallelCount = std::make_shared<std::atomic<size_t>>(batchCount);
        return std::make_pair(
            SettableParallelJobFuture<T>{ ptr,parallelCount },
            ParallelJobFuture{ ptr }
        );
    }

    // 結果なしの通知だけ
    size_t sub_counter() const {
        return counter->fetch_sub(1, std::memory_order_acq_rel);
    }

    void set_value(T v) const {
        std::lock_guard lk(inner->mtx);
        inner->result = std::move(v);
        inner->ready = true;
    }

    void set_exception(std::exception_ptr e) {
        std::lock_guard lk(inner->mtx);
        inner->eptr = std::move(e);
        inner->ready = true;
    }
};

template<>
class SettableParallelJobFuture<void> {
    std::shared_ptr<FutureInner<void>> inner;
    std::shared_ptr<std::atomic<size_t>> counter;
public:
    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<void>> i, std::shared_ptr<std::atomic<size_t>> count)
        : inner(std::move(i)), counter(count) {}

    static auto create(size_t batchCount) {
        auto ptr = std::make_shared<FutureInner<void>>();
        auto parallelCount = std::make_shared<std::atomic<size_t>>(batchCount);
        return std::make_pair(
            SettableParallelJobFuture<void>{ ptr, parallelCount },
            ParallelJobFuture{ ptr }
        );
    }

    // 結果なしの通知だけ
    size_t sub_counter() const {
        return counter->fetch_sub(1, std::memory_order_acq_rel);
    }

    void set_value() const {
        std::lock_guard lk(inner->mtx);
        inner->ready = true;
    }

    void set_exception(std::exception_ptr e) {
        std::lock_guard lk(inner->mtx);
        inner->eptr = std::move(e);
        inner->ready = true;
    }
};

template<typename S>
struct JobConnector {
    using Ptrs = decltype(S::field_ptrs());
    static constexpr size_t N = std::tuple_size_v<Ptrs>;

    Ptrs ptrs_{ S::field_ptrs() };

    // I番目のメンバポインタを取り出し
    template<size_t I>
    constexpr auto get() const noexcept {
        return std::get<I>(ptrs_);
    }
};

template<typename T>
struct ICommand {
    virtual ~ICommand() = default;
    virtual void apply(T& obj) = 0;
};

template<>
struct ICommand<void> {};

//template<typename Struct, std::size_t I>
//struct AssignFieldCmd : ICommand<Struct> {
//    using FieldT = std::tuple_element_t<I, Struct>;
//    FieldT value_;
//
//    AssignFieldCmd(FieldT v) : value_(std::move(v)) {}
//
//    void apply(Struct& s) override {
//        boost::pfr::get<I>(s) = value_;
//    }
//};

template<typename T>
struct FuncCmd : ICommand<T> {
    std::function<void(T&)> fn_;

    FuncCmd(std::function<void(T&)>&& f) : fn_(std::move(f)) {}

    void apply(T& obj) override {
        fn_(obj);
    }
};

// コマンドバッファ：ICommand<T> を蓄積して flush 時にすべて apply
template<typename T>
class CommandBuffer {
    std::vector<std::unique_ptr<ICommand<T>>> cmds_;

    using ICommandPtr = std::unique_ptr<ICommand<T>>;
public:
    void push(ICommandPtr&& c) {
        cmds_.push_back(std::move(c));
    }

    void flush(T& obj) {
        for (auto& cmd : cmds_) {
            cmd->apply(obj);
        }
        cmds_.clear();
    }

};

struct DummyBuffer {};

class JobManager;

template<typename Derived,typename ReturnType,typename JobData = std::monostate>
struct IJob : std::enable_shared_from_this<Derived> {
    using TaskPtr = Ptr::intrusive_ptr<Task>;
    using Data_t = JobData;
    
    using HasData = std::bool_constant<!std::is_same_v<Data_t,std::monostate>>;

    using Return_t = ReturnType;

    using HasReturn = std::bool_constant<!std::is_same_v<Return_t, void>>;

    using Future_t = std::conditional_t<
        HasReturn::value,
        JobFuture<Return_t>,
        JobFuture<void>
    >;

    using Setter_t = std::conditional_t<
        HasReturn::value,
        SettableJobFuture<Return_t>,
        SettableJobFuture<void>
    >;

    using Buffer_t = std::conditional_t<
        HasData::value,
        CommandBuffer<Data_t>,
        DummyBuffer
    >;

    IJob() = default;

    template <typename T = Data_t,
        typename = std::enable_if_t<!std::is_same_v<T, void>>>
        IJob(T&& d)
        : jobResult(std::forward<T>(d)) {}


    ~IJob() {};

    struct Context {
        std::shared_ptr<Derived> self;
        Setter_t setter;

        Context()
            : setter(nullptr, 0) {}

        Context(std::shared_ptr<Derived> s,
            Setter_t&& set)
            : self(s),setter(std::move(set)) {}

        ~Context() {};
    };

    inline std::pair<TaskPtr, Future_t> schedule(JobCategory cat) {
        auto [settable, future] = Setter_t::create();

        //実行前にJob実行の参照データに変更適用
        if constexpr (HasData::value) {
            buffer.flush(jobResult);
        }

        auto ctx = std::make_shared<Context>(
            shared_this()
            , std::move(settable)
            );

        // ワーカー数分だけ Task を作成して登録
        auto work = [ctx]() { workerEntry(ctx); };

        auto task = new Task(Job(std::move(work)), 0,cat);
        TaskPtr taskPtr{ std::move(task) };

        JobManager::Instance().pushJobWaitQueue(taskPtr);

        return std::make_pair(taskPtr, future);
    }

    inline std::pair<TaskPtr,Future_t> schedule(JobCategory cat,const std::vector<TaskPtr>& deps) {
        auto [settable, future] = Setter_t::create();

        //実行前にJob実行の参照データに変更適用
        if constexpr (HasData::value) {
            buffer.flush(jobResult);
        }

        auto ctx = std::make_shared<Context>(
            shared_this()
            , std::move(settable)
            );

        // ワーカー数分だけ Task を作成して登録
        auto work = [ctx]() { workerEntry(ctx); };

        auto task = new Task(Job(std::move(work)), 0,cat);
        TaskPtr taskPtr{ std::move(task) };

        for (auto& d : deps) {
            std::lock_guard<std::mutex> lk(d->taskMutex);
            if (d && d->job.valid()) {
                taskPtr.get()->addDependent(d.get());
            }
        }

        if (taskPtr->inDegree.load() == 0) {
            (taskPtr);
        }

        return std::make_pair(taskPtr,future);
    }

    using ICommandPtr = std::unique_ptr<ICommand<JobData>>;

    inline void AddRequeset(ICommandPtr&& command) {
        if constexpr (HasData::value) {
            buffer.push(std::move(command));
        }
    }

private:
    static void workerEntry(std::shared_ptr<Context> ctx) {
        auto self = ctx->self;
        if (!self) {
            std::printf("self is nullptr");
            return;
        }
        
        //帰り値あり
        try {
            if constexpr (HasReturn::value) {
                if constexpr (std::is_same_v<Return_t, Data_t>) {
                    self->Execute();
                    ctx->setter.set_value(self->jobResult); // 値渡し
                }
                else {
                    // Execute の結果をその場で値渡し
                    ctx->setter.set_value(self->Execute());
                }
            }
            else {
                // void 戻りの場合
                self->Execute();
                ctx->setter.set_value();
            }
        }
        catch (...) {
            ctx->setter.set_exception(std::current_exception());
        }
    }

    inline void Execute() {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute(size_t) member function!!");
    }

protected:
    std::shared_ptr<Derived> shared_this()
    {
        return std::enable_shared_from_this<Derived>::shared_from_this();
    }

    [[no_unique_address]] Data_t jobResult;

private:
    std::atomic<size_t> nextBatch_{ 0 };

    Buffer_t buffer;
};

//parallelJobの基底クラス
//Derived : 派生クラス
//ParallelJoData : Job実行時に書き込むデータ
template<typename Derived,typename ParallelJobData  = std::monostate>
struct IParallelJob : std::enable_shared_from_this<Derived>{
    using TaskPtr = Ptr::intrusive_ptr<Task>;

    using Data_t = ParallelJobData;

    using HasData = std::bool_constant<!std::is_same_v<Data_t, std::monostate>>;

    using Future_t = std::conditional_t<
        HasData::value,
        ParallelJobFuture<Data_t>,
        ParallelJobFuture<void>
        >;

    using Setter_t = std::conditional_t<
        HasData::value,
        SettableParallelJobFuture<Data_t>,
        SettableParallelJobFuture<void>
        >;

    using Buffer_t = std::conditional_t<
        HasData::value,
        CommandBuffer<Data_t>,
        DummyBuffer
        > ;

    IParallelJob() = default;

    template <typename T = Data_t,
        typename = std::enable_if_t<!std::is_same_v<T, void>>>
        IParallelJob(T&& d)
        : jobResult(std::forward<T>(d)) {}
    
    ~IParallelJob(){};

    struct Context {
        std::shared_ptr<Derived> self;
        size_t total, batchSize, numBatches, chunkBatches;
        std::atomic<size_t>* nextBatch;
        Setter_t setter;

        Context(std::atomic<size_t>* next)
            : total(0), batchSize(0), numBatches(0), chunkBatches(0),nextBatch(next), setter(nullptr,0) {}

         Context(std::shared_ptr<Derived> s,
            size_t t, size_t b, size_t n,
            size_t c,
            std::atomic<size_t>* next,
            Setter_t&& set)
      : self(s),total(t),batchSize(b),numBatches(n), chunkBatches(c),nextBatch(next),setter(std::move(set)){}

        ~Context(){};
    };

    inline std::pair<std::vector<TaskPtr>, Future_t> schedule(JobCategory cat, const size_t total,const size_t batchSize,const size_t workerCount) {
        ASSERT(total > 0 && batchSize > 0, "parallelJob schedule total or batchSize is zero");

        const size_t numBatches = (total + batchSize - 1) / batchSize;

        const size_t threadSize = JobManager::Instance().getThreadSize();
        const size_t workerNum = std::min({threadSize,workerCount,numBatches});

        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));

        auto [settable, future] = Setter_t::create(numBatches);

        //実行前にJob実行の参照データに変更適用
        if constexpr (HasData::value) {
            buffer.flush(jobResult);
        }

        nextBatch_.store(0, std::memory_order_relaxed);

        auto ctx = std::make_shared<Context>(
            shared_this()
            , total
            , batchSize
            , numBatches
            , chunkBatches
            , &nextBatch_
            , std::move(settable)
            );

        std::vector<TaskPtr>tasks;
        tasks.reserve(workerNum);

        // ワーカー数分だけ Task を作成して登録
        for (size_t w = 0; w < workerNum; ++w) {
            auto work = [ctx]() { workerEntry(ctx); };

            auto task = new Task(Job(std::move(work)), 0,cat);
            TaskPtr taskPtr{ std::move(task) };

            JobManager::Instance().pushJobWaitQueue(taskPtr);

            tasks.push_back(taskPtr);
        }

        return std::make_pair(tasks, future);
    }

    inline std::pair<std::vector<TaskPtr>,Future_t> schedule(JobCategory cat,size_t total,size_t batchSize,size_t workerCount,const std::vector<TaskPtr>&deps){
        ASSERT(total > 0 && batchSize > 0&& workerCount > 0,"parallelJob schedule total or batchSize is zero");

        const size_t numBatches = (total + batchSize - 1) / batchSize;
        const size_t threadSize = JobManager::Instance().getThreadSize();
        const size_t workerNum = std::min({ threadSize,workerCount,numBatches });

        size_t chunkBatches = std::clamp(numBatches / (8 * workerNum), size_t(1), size_t(64));

        auto [settable, future] = Setter_t::create(numBatches);

        //実行前にJob実行の参照データに変更適用
        if constexpr (HasData::value){
            buffer.flush(jobResult);
        }
        
        nextBatch_.store(0, std::memory_order_relaxed);

        auto ctx = std::make_shared<Context>(
            shared_this()
            ,total
            ,batchSize
            ,numBatches
            ,chunkBatches
            ,&nextBatch_
            ,std::move(settable)
            );

        std::vector<TaskPtr>tasks;
        tasks.reserve(workerNum);

        // ワーカー数分だけ Task を作成して登録
        for (size_t w = 0; w < workerCount; ++w) {
            auto work = [ctx]() { workerEntry(ctx); };

            auto task = new Task(Job(std::move(work)), 0,cat);
            TaskPtr taskPtr{std::move(task)};

            for (auto& d : deps) {
                std::lock_guard<std::mutex> lk(d->taskMutex);
                if (d && d->job.valid()) {
                    taskPtr.get()->addDependent(d.get());
                }
            }

            if (taskPtr->inDegree.load() == 0) {
                JobManager::Instance().pushJobWaitQueue(taskPtr);
            }

            tasks.push_back(taskPtr);
        }

        return std::make_pair(tasks,future);
    }

    using ICommandPtr = std::unique_ptr<ICommand<ParallelJobData>>;

    inline void AddRequeset(ICommandPtr&& command){
        if constexpr (HasData::value){
            buffer.push(std::move(command));
        }
    }
   
private:
    static void workerEntry(std::shared_ptr<Context> ctx) {
        auto self = ctx->self;
        if(!self){
            std::printf("self is nullptr");
            return;
        }
        const size_t batchSize = ctx->batchSize;
        const size_t numBatches = ctx->numBatches;
        const size_t total = ctx->total;
        const size_t chunkBatches = ctx->chunkBatches;

        while (true) {
            //回数見直し、バッチ回数に合わせる。応じて変える
            //numBatchesも
            const size_t idx = ctx->nextBatch->fetch_add(chunkBatches, std::memory_order_relaxed);

            if(idx >= numBatches) return;

            const size_t taken = std::min(chunkBatches, numBatches - idx);

            for (size_t b = 0; b < taken; ++b) {
                const size_t start = (idx + b) * batchSize;

                ASSERT(start < total,"IParallelJob workerEntry : start >= total");

                const size_t len = std::min(batchSize, total - start);

                self->ExecuteBatch(start,len,self.get());

                if (ctx->setter.sub_counter() == 1) {
                    if constexpr (HasData::value){
                        ctx->setter.set_value(self->jobResult);
                    }else{
                        ctx->setter.set_value();
                    }
                    
                    return;
                }
            }
        }
    }

    inline void Execute(size_t index) {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute(size_t) member function!!");
    }

protected:
    inline void ExecuteBatch(const size_t start,const size_t len,Derived*self) {
        for (size_t i = start; i < start + len; ++i) {
            self->Execute(i);
        }
    }

    std::shared_ptr<Derived> shared_this()
    {
        return std::enable_shared_from_this<Derived>::shared_from_this();
    }

    [[no_unique_address]] Data_t jobResult;

private:
    std::atomic<size_t> nextBatch_{ 0 };

    Buffer_t buffer;

    size_t threadSize;
};

}//namespace ECS::JobSystem
