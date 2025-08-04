#pragma once

#include <utility>   // std::swap, std::exchange
#include <cstddef>   // std::nullptr_t
#include "JobManager.h"
#include "TestFramework.hpp"

namespace ECS::JobSystem{

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
        if (ptr_) intrusive_ptr_add_ref(ptr_);
    }

    // コピー：参照カウンタ +1
    intrusive_ptr(intrusive_ptr const& o) noexcept
        : ptr_(o.ptr_) {
        if (ptr_) intrusive_ptr_add_ref(ptr_);
    }

    // ムーブ：コピー先だけ ptr を奪う（参照カウントは変えない）
    intrusive_ptr(intrusive_ptr&& o) noexcept
        : ptr_(o.ptr_) {
        o.ptr_ = nullptr;
    }

    // デストラクタ：参照カウント -1、0 なら delete
    ~intrusive_ptr() noexcept {
        if (ptr_) intrusive_ptr_release(ptr_);
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

    }

    //
    
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
            invoke_fn = nullptr;
        }
    }

    bool valid() const noexcept {
        return invoke_fn != nullptr;
    }

    // 暗黙の bool 変換は禁止
    explicit operator bool() const noexcept = delete;
};

struct Task {
    std::atomic<uint32_t> refCount{ 0 };
    Job job;
    std::atomic<int>   inDegree{ 0 };
    Ptr::intrusive_ptr<Task> nextDependent;
    std::mutex taskMutex;

    Task(Job jb, int degree)
        : refCount(0)
        , job(std::move(jb))
        , inDegree(degree)
        , nextDependent(nullptr)
    {}

    void add_ref() { refCount.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

inline void intrusive_ptr_add_ref(Task* p) { p->add_ref(); }
inline void intrusive_ptr_release(Task* p) { p->release(); }

// 未 specialization：結果を持てる型用
template<typename T>
struct FutureInner {
    std::mutex       mtx;
    std::atomic<bool> ready{ false };
    std::optional<T>  result;
};

// void 専用 specialization：result を持たない
template<>
struct FutureInner<void> {
    std::mutex       mtx;
    std::atomic<bool> ready{ false };
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
            {
                std::lock_guard lk(inner->mtx);
                if (inner->ready) {
                    static_assert(!std::is_void_v<T>, "JobFuture<T> is void");

                    return std::move(*inner->result);
                }
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
            {
                if (inner->ready) {
                    return;
                }
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
        return;
    }

    T wait_and_get() {
        while (true) {
            // まず mutex を獲得して ready フラグをチェック
            {
                if (isReady()) {
                    static_assert(!std::is_void_v<T>, "JobFuture<T> is void");

                    return std::move(*inner->result);
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
};




template<typename T>
class SettableParallelJobFuture {
    std::shared_ptr<FutureInner<T>> inner;
    std::shared_ptr<std::atomic<size_t>> counter;
public:
    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<T>> i, std::shared_ptr<std::atomic<size_t>> count)
        : inner(std::move(i)),counter(count) {}

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

    void set_value(T& v) const {
        std::lock_guard lk(inner->mtx);
        inner->result = v;
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
        if (counter->fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lk(inner->mtx);
            inner->ready = true;
        }
    }
};

//template<typename Derived, size_t BufferSize>
//struct ParallelJobCRTP {
//    // 小型バッファ（SBO 用）
//    alignas(std::max_align_t) char buf[BufferSize];
//
//    size_t begin{}, len{};
//
//    ParallelJobCRTP() = default;
//
//    // Fn はトリビアルコピー・破棄可能と仮定
//    template<typename F>
//    ParallelJobCRTP(F&& f, size_t b, size_t l) noexcept {
//        using Fn = std::decay_t<F>;
//        static_assert(sizeof(Fn) <= BufferSize, "バッファサイズ不足");
//        static_assert(std::is_trivially_copyable_v<Fn> &&
//            std::is_trivially_destructible_v<Fn>,
//            "Fn はトリビアルである必要があります");
//
//        new (buf) Fn(std::forward<F>(f));
//        // Buf 内の F を Derived と見なして扱います
//        // （Derived::execute() の中で F のオブジェクトを取り出して呼び出す設計）
//        begin = b; len = l;
//    }
//
//    // invoke では常に static dispatch
//    void invoke() noexcept {
//        // Derived 側で buf の中身（=F）を execute() 内部で呼び出す
//        static_cast<Derived*>(this)->execute(begin, len);
//    }
//};


//template<size_t BufferSize = 32>
//struct ParallelJob {
//    using Func = std::function<void(size_t)>;
//    alignas(void*) char buf[BufferSize];
//
//    void (*invoke_fn)(void*, size_t) = nullptr;
//
//    size_t total;
//    size_t batchSize;
//    size_t numBatches;
//
//    std::atomic<size_t>  nextBatch{ 0 };
//
//    ParallelJob() = default;
//    ~ParallelJob() {
//        if (destroy_fn) destroy_fn(buf);
//    }
//
//    ParallelJob(ParallelJob&& o)noexcept{
//        std::memcpy(buf,o.buf,BufferSize);
//        invoke_fn = o.invoke_fn;
//        begin = o.begin;
//        len = o.len;
//
//        o.invoke_fn = nullptr;
//    }
//
//    ParallelJob& operator=(ParallelJob&& o) noexcept{
//        if(this!= &o){
//            if(destroy_fn)destroy_fn(buf);
//            std::memcpy(buf, o.buf, BufferSize);
//            invoke_fn = o.invoke_fn;
//            destroy_fn = o.destroy_fn;
//            begin = o.begin;
//            len = o.len;
//
//            o.invoke_fn = nullptr;
//        }
//        return *this;
//    }
//
//    template<typename F>
//    ParallelJob(F&& f,size_t b,size_t l)noexcept{
//        using Fn = std::decay_t<F>;
//        static_assert(sizeof(Fn) <= BufferSize,"ParallelJob function too large");
//
//        new (buf) Fn(std::forward<F>(f));
//
//        invoke_fn = [](void* p, size_t bb, size_t ll) {
//            auto fp = static_cast<Fn*>(p);
//            (*fp)(bb, ll);
//        };
//
//        destroy_fn = [](void* p) {
//            static_cast<Fn*>(p)->~Fn();
//        };
//
//        begin = b; len = l;
//    }
//
//    void invoke() noexcept {
//        if (invoke_fn) {
//            ASSERT(invoke_fn,"invoke is nullptr");
//            invoke_fn(buf, begin, len);
//            invoke_fn = nullptr;  // 1 回だけ
//        }
//    }
//
//    template<typename F>
//    static ParallelJob create(F&& func,
//        size_t total,
//        size_t batchSize)
//    {
//        static_assert(std::is_invocable_v<F, size_t>,
//            "create()のfuncはsize_tを受け取れるcallableでなければなりません");
//
//        static_assert(sizeof(Func) <= BufferSize,
//            "BufferSize が足りません");
//        
//        ParallelJob job;
//        // バッファ上に Func を構築
//        new (job.buf) Func(std::move(func));
//        // invoke_fn をセット
//        job.invoke_fn = [](void* p, size_t idx) {
//            auto& userFunc = *reinterpret_cast<Func*>(p);
//            userFunc(idx);
//        };
//
//        new (buf) F(std::move(func));
//        job.invoke_fn = [](void* p,size_t idx) {
//            auto fp = static_cast<F*>(p,idx);
//            (*fp)(idx);
//        };
//
//        job.destroy_fn = [](void* p,size_t idx) {
//            static_cast<F*>(p,idx)->~F();
//        };
//
//        job.total = total;
//        job.batchSize = batchSize;
//        job.numBatches = (total + batchSize - 1) / batchSize;
//
//        return job;
//
//        for (size_t b = 0; b < numBatches; ++b) {
//            size_t begin = b * batchSize;
//            size_t len = min(batchSize, total - begin);
//
//            jobs.emplace_back(
//                // 範囲ループするだけのラムダ
//                [f = std::forward<F>(func),setterPtr](size_t bb, size_t ll) {
//
//                    for (size_t i = bb; i < bb + ll; ++i) {
//                        f(i);
//                    }
//
//                    setterPtr->set_value();
//                },
//                begin, len
//                    );
//        }
//
//        return job;
//    }
//
//    ParallelJobFuture schedule(size_t total,size_t batchSize,size_t workerCount) {
//        static_assert(total > 0 && batchSize > 0);
//
//        auto [settable, future] = SettableParallelJobFuture::create(batchSize);
//        auto setterPtr = std::make_shared<SettableParallelJobFuture>(std::move(settable));
//
//        for (size_t i = 0; i < workerCount; ++i) {
//            
//            TaskPtr t{new Task(
//                Job([this, setterPtr]() mutable {
//                while (true) {
//                    size_t idx = nextBatch.fetch_add(1, std::memory_order_relaxed);
//                    if (idx >= numBatches) break;
//
//                    size_t begin = idx * batchSize;
//                    size_t len = std::min(batchSize, total - begin);
//                    func(begin, len);
//
//                    setterPtr.set_value();
//                }
//            })};
//
//            JobManager::Instance().pushWaitQueue(t);
//        }
//
//        for (uint32_t i = 0; i < jobsPtr->size(); ++i) {
//
//            TaskPtr t{ new Task(
//                Job([jobsPtr,i]() mutable {
//                        (*jobsPtr)[i].invoke();
//                }
//                ),
//                0
//            ) };
//
//            
//        }
//
//        return future;
//    }
//};

template<typename... Containers>
struct JobConnector {

    using StorageTuple = std::tuple<std::decay_t<Containers>...>;

    // コンストラクタ：各コンテナを参照渡し
    JobConnector(Containers&&... conts)
        : storage_(std::forward<Containers>(conts)...)
        , length_{ sizeof...(Containers) }
    {
        static_assert(sizeof...(Containers) > 0,
            "At least one container is required");
    }

    // 要素数取得
    std::size_t length() const noexcept { return length_; }

    // I番目の生ポインタ取得
    template<std::size_t I>
    auto* pointer() noexcept {
        return std::get<I>(storage_);

    }

    template<std::size_t I>
    const auto* pointer() const noexcept {
        return std::get<I>(storage_);
    }

    // I番目コンテナの idx要素を参照で取得
    template<std::size_t I>
    auto& get() {
        return std::get<I>(storage_);
    }

    template<std::size_t I>
    const auto& get() const {
        //assert(I < length_);
        return std::get<I>(storage_);
    }

private:
    StorageTuple storage_;
    std::size_t length_;
};

class JobManager;

template<typename Derived,typename ParallelJobData>
struct IParallelJob{

    IParallelJob() = default;

    IParallelJob(ParallelJobData&& d): jobResult(std::move(d)){
        self = static_cast<Derived*>(this);
    }

    struct Context {
        Derived* self;
        ParallelJobData* result;
        size_t   total, batchSize, numBatches;
        std::atomic<size_t>* nextBatch;
        SettableParallelJobFuture<ParallelJobData> setter;

        Context(Derived* s,
            ParallelJobData* r,
            size_t t,
            size_t b,
            size_t n,
            std::atomic<size_t>* next,
            SettableParallelJobFuture<ParallelJobData>&& set)
            : self(s)
            , result(r)
            , total(t)
            , batchSize(b)
            , numBatches(n)
            , nextBatch(next)
            , setter(std::move(set))
        {}

    };

    inline ParallelJobFuture<ParallelJobData> schedule(size_t total,size_t batchSize,size_t workerCount){
        ASSERT(total > 0 && batchSize > 0,"parallelJob schedule total or batchSize is zero");

        auto [settable, future] = SettableParallelJobFuture<ParallelJobData>::create(batchSize);

        size_t numBatches = (total + batchSize - 1) / batchSize;
        auto ctx = std::make_shared<Context>(
            self,
            &jobResult,
            total,
            batchSize,
            numBatches,
            &nextBatch_,
            std::move(settable)
        );

        nextBatch_.store(0, std::memory_order_relaxed);

        // ワーカー数分だけ Task を作成して登録
        for (size_t w = 0; w < workerCount; ++w) {
            auto work = [ctx]() { workerEntry(ctx); };

            auto task = new Task(Job(std::move(work)), 0);
            Ptr::intrusive_ptr taskPtr{std::move(task)};

            JobManager::Instance().pushWaitQueue(std::move(taskPtr));
        }

        return future;
    }

    inline void Execute(size_t index) {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute(size_t) member function!!");
    }

protected:
    ParallelJobData jobResult;
   
private:
    static void workerEntry(std::shared_ptr<Context> ctx) {
        while (true) {
            auto idx = ctx->nextBatch->fetch_add(1, std::memory_order_relaxed);
            if (idx >= ctx->numBatches) {
                break;
            }

            size_t start = idx * ctx->batchSize;
            size_t len = min(ctx->batchSize, ctx->total - start);

            for (size_t i = start; i < start + len; ++i) {
                ctx->self->Execute(i);
            }

            if (ctx->setter.sub_counter() == 1) {
                ctx->setter.set_value(*ctx->result);
                break;
            }
        }
    }

private:
    std::atomic<size_t>     nextBatch_{ 0 };
    Derived* self;
};

}//namespace ECS::JobSystem
