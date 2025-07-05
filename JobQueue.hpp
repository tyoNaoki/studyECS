#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>

namespace ECS::JobSystem {

    template<typename T>
    class ChaseLevDeque {
        static_assert(std::is_nothrow_move_constructible<T>::value,
            "T must be nothrow-move-constructible");

    public:
        explicit ChaseLevDeque(size_t capacity)
            : bottom(0),
            top(0),
            tail(0),
            capacity(capacity),
            mask(capacity - 1),
            slotMutex(capacity),
            slotData(capacity)
        {}
                
        //~ChaseLevDeque() { ::operator delete[](buffer_); }

        // pushBottom: オーナースレッドのみ
        bool pushBottom(T&& task) {
            size_t b = bottom;
            size_t idx = b & mask;

            std::unique_lock<std::mutex> lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock() || slotData[idx].has_value())
                return false;

            slotData[idx] = std::move(task);
            bottom = b + 1;
            return true;
        }

        // popBottom: オーナースレッドのみ
        std::optional<T> popBottom() {
            size_t b0 = bottom;
            size_t t0 = top.load(std::memory_order_acquire);
            if (t0 >= b0)
                return std::nullopt;

            size_t b1 = b0 - 1;
            bottom = b1;

            size_t idx = b1 & mask;
            std::lock_guard<std::mutex> lk(slotMutex[idx]);
            if (!slotData[idx].has_value()) {
                bottom = b0;  // 元に戻す
                return std::nullopt;
            }

            T result = std::move(*slotData[idx]);
            slotData[idx].reset();

            if (t0 == b1) {
                size_t expected = t0;
                if (top.compare_exchange_strong(expected, t0 + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                    bottom = b0;  // 底も進める
                }
                else {
                    bottom = expected + 1;
                    return std::nullopt;
                }
            }
            return result;
        }

        // stealTop: 他スレッド
        std::optional<T> stealTop() {
            std::lock_guard<std::mutex> lkTail(tailMutex);
            size_t t = tail;
            if (t >= bottom)
                return std::nullopt;

            size_t idx = t & mask;
            std::lock_guard<std::mutex> lkSlot(slotMutex[idx]);
            if (!slotData[idx].has_value())
                return std::nullopt;

            T result = std::move(*slotData[idx]);
            slotData[idx].reset();
            tail = t + 1;
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

        std::vector<std::mutex>        slotMutex;
        std::vector<std::optional<T>>  slotData;

        // インデックス管理
        std::atomic<size_t> top;     // スティーラーが進める
        size_t            bottom;    // オーナーのみ
        size_t            tail;      // stealTop 用位置
        const size_t      capacity;
        const size_t      mask;      // capacity は 2^N の前提

        std::mutex        tailMutex; // tail 更新用

    };

}  // namespace ECS::JobSystem
