#pragma once

#include <utility>   // std::swap, std::exchange
#include <cstddef>   // std::nullptr_t

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
            // まず mutex を獲得して ready フラグをチェック
            {
                std::lock_guard lk(inner->mtx);
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
        return;
    }

    bool isReady() const override {
        return inner->ready;
    }

private:
    std::shared_ptr<FutureInner<void>> inner;
};

struct ParallelJobFuture : public IFuture {
    explicit ParallelJobFuture(std::shared_ptr<FutureInner<void>> i)
        : inner(std::move(i)) {}

    void wait() override {
        return;
    }

    bool isReady() const override {
        return inner->ready;
    }

private:
    std::shared_ptr<FutureInner<void>> inner;
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

class SettableParallelJobFuture {
    std::shared_ptr<FutureInner<void>> inner;
    std::shared_ptr<std::atomic<size_t>> counter;
public:
    explicit SettableParallelJobFuture(std::shared_ptr<FutureInner<void>> i, std::shared_ptr<std::atomic<size_t>> count)
        : inner(std::move(i)),counter(count) {}

    static auto create(size_t batchCount) {
        auto ptr = std::make_shared<FutureInner<void>>();
        auto parallelCount = std::make_shared<std::atomic<size_t>>(batchCount);
        return std::make_pair(
            SettableParallelJobFuture{ ptr,parallelCount },
            ParallelJobFuture{ ptr }
        );
    }

    // 結果なしの通知だけ
    void set_value() const {
        std::lock_guard lk(inner->mtx);
        if(counter->fetch_sub(1) == 1){
            inner->ready = true;
        }
    }
};

template<size_t BufferSize = 32>
struct ParallelJob {
   
    alignas(void*) char buf[BufferSize];

    void (*invoke_fn)(void*, size_t, size_t) = nullptr;
    void (*destroy_fn)(void*) = nullptr;

    size_t begin = 0, len = 0;

    ParallelJob() = default;
    ~ParallelJob() {
        if (destroy_fn) destroy_fn(buf);
    }

    ParallelJob(ParallelJob&& o)noexcept{
        std::memcpy(buf,o.buf,BufferSize);
        invoke_fn = o.invoke_fn;
        destroy_fn = o.destroy_fn;
        begin = o.begin;
        len = o.len;

        o.invoke_fn = nullptr;
        o.invoke_fn = nullptr;
    }

    ParallelJob& operator=(ParallelJob&& o) noexcept{
        if(this!= &o){
            if(destroy_fn)destroy_fn(buf);
            std::memcpy(buf, o.buf, BufferSize);
            invoke_fn = o.invoke_fn;
            destroy_fn = o.destroy_fn;
            begin = o.begin;
            len = o.len;

            o.invoke_fn = nullptr;
            o.destroy_fn = nullptr;
        }
        return *this;
    }

    template<typename F>
    ParallelJob(F&& f,size_t b,size_t l)noexcept{
        using Fn = std::decay_t<F>;
        static_assert(sizeof(Fn) <= BufferSize,"ParallelJob function too large");

        new (buf) Fn(std::forward<F>(f));

        invoke_fn = [](void* p, size_t bb, size_t ll) {
            auto fp = static_cast<Fn*>(p);
            (*fp)(bb, ll);
        };
        destroy_fn = [](void* p) {
            static_cast<Fn*>(p)->~Fn();
        };
        begin = b; len = l;
    }

    void invoke() noexcept {
        if (invoke_fn) {
            ASSERT(invoke_fn,"invoke is nullptr");
            invoke_fn(buf, begin, len);
            invoke_fn = nullptr;  // 1 回だけ
        }
    }

    template<typename F>
    static auto create(F&&func,size_t total,size_t batchSize)
    {
        size_t numBatches = (total + batchSize - 1) / batchSize;
        std::vector<ParallelJob> jobs;
        jobs.reserve(numBatches);
        
        auto [settable, future] = SettableParallelJobFuture::create(batchSize);
        auto setterPtr = std::make_shared<SettableParallelJobFuture>(std::move(settable));

        for (size_t b = 0; b < numBatches; ++b) {
            size_t begin = b * batchSize;
            size_t len = min(batchSize, total - begin);

            jobs.emplace_back(
                // 範囲ループするだけのラムダ
                [f = std::forward<F>(func),setterPtr](size_t bb, size_t ll) {


                    for (size_t i = bb; i < bb + ll; ++i) {
                        f(i);
                    }

                    setterPtr->set_value();
                },
                begin, len
                    );
        }

        return jobs;
    }
};

}//namespace ECS::JobSystem
