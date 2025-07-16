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

    // ← ここで必要になるのがムーブ代入演算子
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

}//namespace ECS::JobSystem
