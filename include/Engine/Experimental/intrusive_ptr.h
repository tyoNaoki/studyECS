#pragma once

namespace ECS::JobSystem{

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
            if (ptr_) ptr_->add_ref();
        }

        // コピー：参照カウンタ +1
        intrusive_ptr(intrusive_ptr const& o) noexcept
            : ptr_(o.ptr_) {
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

}
