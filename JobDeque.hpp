#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>

namespace ECS::JobSystem {

    template<typename T>
    class JobDeque {
        static_assert(std::is_nothrow_move_constructible<T>::value,
            "T must be nothrow-move-constructible");

    public:
        explicit JobDeque(size_t capacity)
            : bottom(0),
            top(0),
            capacity(capacity),
            mask(capacity - 1),
            slotMutex(capacity),
            slotData(capacity)
        {
            if ((capacity & (capacity - 1)) != 0) {
                throw std::invalid_argument("capacity must be a power of 2");
            }

            // capacity はゼロ以上でなければならない
            if (capacity == 0) {
                throw std::invalid_argument("capacity must be greater than zero");
            }
        }
                
        ~JobDeque() = default;

        JobDeque(const JobDeque&) = delete;  // コピー禁止
        JobDeque& operator=(const JobDeque&) = delete;
        JobDeque(JobDeque&&) noexcept = default; // ムーブのみ OK
        JobDeque& operator=(JobDeque&&) noexcept = default;

        //pushBottom:オーナースレッドのみ
        bool pushBottom(T task) {
            size_t b = bottom;
            size_t idx = b & mask;

            std::unique_lock<std::mutex> lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock() || slotData[idx].has_value())
                return false;

            slotData[idx] = task;
            bottom = b + 1;
            return true;
        }

        //popBottom:オーナースレッドのみ
        std::optional<T> popBottom() {
            size_t b0 = bottom.load(std::memory_order_relaxed);
            size_t t0 = top.load(std::memory_order_acquire);
            if (t0 >= b0)
                return std::nullopt;

            size_t b1 = b0 - 1;
            bottom.store(b1, std::memory_order_release);

            size_t idx = b1 & mask;
            std::lock_guard<std::mutex> lk(slotMutex[idx]);
            if (!slotData[idx].has_value()) {
                //元に戻す
                bottom.store(b0, std::memory_order_release);
                return std::nullopt;
            }

            T result = std::move(*slotData[idx]);
            slotData[idx].reset();

            if (t0 == b1) {
                size_t expected = t0;
                if (top.compare_exchange_strong(expected, t0 + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                    bottom.store(b0, std::memory_order_release); 
                }
                else {
                    bottom.store(expected + 1, std::memory_order_release);
                    return std::nullopt;
                }
            }

            checkInvariants();
            return result;
        }

        //他スレッドからのstealTop
        std::optional<T> stealTop() {
            //topを読み出し
            size_t t0 = top.load(std::memory_order_acquire);
            size_t b = bottom.load(std::memory_order_acquire);
            if (t0 >= b)
                return std::nullopt;    // 空

            size_t idx = t0 & mask;
            T result;
            {
                std::lock_guard<std::mutex> lk(slotMutex[idx]);
                if (!slotData[idx].has_value())
                    return std::nullopt;
                
                result = std::move(*slotData[idx]);
                slotData[idx].reset();
            }

            size_t expected = t0;
            if (!top.compare_exchange_strong(
                expected, t0 + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed))
            {
                //他者が同じ要素を奪った可能性あり
                return std::nullopt;
            }

            return result;
        }



        bool empty() const {
            size_t b = bottom;
            size_t t = top.load(std::memory_order_acquire);
            return t >= b;
        }

        // スレッドセーフではありません。デバッグ用にのみ。
        size_t unsafe_size() const {
            return bottom - top.load();
        }

    private:
        void checkInvariants() {
            size_t t = top.load(std::memory_order_acquire);
            size_t b = bottom;
            assert(t <= b);
            assert(b - t <= capacity);
            // slotData の参照カウントと実際の occupied を一致させる
            size_t occ = 0;
            for (auto& opt : slotData) if (opt.has_value()) ++occ;
            assert(occ == b - t);
        }



    private:

        std::vector<std::mutex>        slotMutex;
        std::vector<std::optional<T>>  slotData;

        // インデックス管理
        std::atomic<size_t> top;     // スティーラーが進める
        std::atomic<size_t> bottom;    // オーナーのみ
        const size_t      capacity;
        const size_t      mask;      // capacity は 2^N の前提

    };

}  // namespace ECS::JobSystem
