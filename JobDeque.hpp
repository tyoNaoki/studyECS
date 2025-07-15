#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>
#include "taskPtr.hpp"

namespace ECS::JobSystem {

    using TaskPtr = Ptr::intrusive_ptr<Task>;

    // pushBottom の結果
    enum class PushStatus {
        Success,    // 正常に push できた
        WouldBlock, // ロック中で進めない
        Full        // バッファがすでに満杯
    };

    enum class PopStatus {
        Success,    // 正常に push できた
        WouldBlock, // ロック中で進めない
        Empty        // 空
    };

    enum class StealStatus {
        Success,    // 正常に push できた
        WouldBlock, // ロック中で進めない
        Empty        // 空
    };

    struct PushResult {
        PushStatus status;
        // not_pushed を返せるようにムーブ前のタスクを保持
        TaskPtr notPushed;
    };

    struct PopResult {
        PopStatus status;
        std::optional<TaskPtr> value;  // 成功時だけ value.has_value()==true
    };

    struct StealResult {
        StealStatus status;
        std::optional<TaskPtr> value;
    };

    class JobDeque {

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
        PushResult pushBottom(TaskPtr task) {
            size_t b = bottom;
            size_t idx = b & mask;

            std::unique_lock<std::mutex> lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock())
                return { PushStatus::WouldBlock,std::move(task)};

            if(slotData[idx].has_value()){
                return { PushStatus::Full, std::move(task) };
            }

            slotData[idx] = task;
            bottom = b + 1;
            return { PushStatus::Success, {} };
        }

        //popBottom:オーナースレッドのみ
        PopResult popBottom() {
            size_t b0 = bottom.load(std::memory_order_relaxed);
            size_t t0 = top.load(std::memory_order_acquire);
            if (t0 >= b0)
                return { PopStatus::Empty, std::nullopt };

            size_t b1 = b0 - 1;
            bottom.store(b1, std::memory_order_release);
            size_t idx = b1 & mask;

            std::unique_lock<std::mutex> lk(slotMutex[idx], std::try_to_lock);

            if(!lk.owns_lock()){
                bottom.store(b0, std::memory_order_release);
                return { PopStatus::WouldBlock, std::nullopt };
            }

            if (!slotData[idx].has_value()) {
                //元に戻す
                bottom.store(b0, std::memory_order_release);
                assert(false,"slotData have nullptr");
                return { PopStatus::Empty, std::nullopt };
            }

            TaskPtr result = std::move(*slotData[idx]);
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
                    return { PopStatus::Empty, std::nullopt };
                }
            }

            checkInvariants();
            return { PopStatus::Success, std::optional<TaskPtr>{std::move(result)} };
        }

        //他スレッドからのstealTop
        StealResult stealTop() {
            //topを読み出し
            size_t t0 = top.load(std::memory_order_acquire);
            size_t b = bottom.load(std::memory_order_acquire);
            if (t0 >= b)
                return { StealStatus::Empty, std::nullopt };    // 空

            size_t idx = t0 & mask;
            std::unique_lock<std::mutex> lk(slotMutex[idx], std::try_to_lock);
            if(!lk.owns_lock()){
                return {StealStatus::WouldBlock,std::nullopt};
            }

            if(!slotData[idx].has_value()){
                return { StealStatus::Empty, std::nullopt };
            }

            TaskPtr result = std::move(*slotData[idx]);
            slotData[idx].reset();

            size_t expected = t0;

            //最後の一要素の場合
            if (!top.compare_exchange_strong(
                expected, t0 + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed))
            {
                //他者が同じ要素を奪った可能性あり
                return { StealStatus::Empty, std::nullopt };
            }

            return { StealStatus::Success, std::optional<TaskPtr>{std::move(result)} };
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
        std::vector<std::optional<TaskPtr>>  slotData;

        // インデックス管理
        std::atomic<size_t> top;     // スティーラーが進める
        std::atomic<size_t> bottom;    // オーナーのみ
        const size_t      capacity;
        const size_t      mask;      // capacity は 2^N の前提

    };

}  // namespace ECS::JobSystem
