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
        explicit JobDeque(size_t capacity,size_t index)
            : bottom(0),
            top(0),
            capacity(capacity),
            mask(capacity - 1),
            slotMutex(capacity),
            slotData(capacity),
            queueIndex(index)
        {
            ASSERT(capacity > 0 && (capacity & (capacity - 1)) == 0 , "capacity must be power of two");
        }

        ~JobDeque() = default;

        JobDeque(const JobDeque&) = delete;  // コピー禁止
        JobDeque& operator=(const JobDeque&) = delete;
        JobDeque(JobDeque&&) noexcept = default; // ムーブのみ OK
        JobDeque& operator=(JobDeque&&) noexcept = default;

        PushResult pushBottom(TaskPtr job) {
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            size_t t0 = top.load(std::memory_order_seq_cst);

            if (b0 - t0 >= capacity) {
                return { PushStatus::Full, std::move(job) };
            }

            size_t idx = b0 & mask;

            // 4) スロットロック
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock()) {
                return { PushStatus::WouldBlock, std::move(job) };
            }

            if (slotData[idx].has_value()) {
                return { PushStatus::Full, std::move(job) };
            }

            slotData[idx] = std::move(job);
            bottom.store(b0 + 1, std::memory_order_seq_cst);

            checkInvariant("PUSH");

            return { PushStatus::Success, {} };
        }

        //―――――――――――――――――――――――――――――――――――
        // オーナースレッド専用：ボトムから pop
        PopResult popBottom() {
            // 1) empty 判定 (要素数==0 のみ)
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            size_t t0 = top.load(std::memory_order_seq_cst);
            if (b0 <= t0) {
                return { PopStatus::Empty, std::nullopt };
            }

            // 2) 取り出し候補位置
            size_t b1 = b0 - 1;
            size_t idx = b1 & mask;

            // 3) スロットロック＋中身チェック
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock()) {
                return { PopStatus::WouldBlock, std::nullopt };
            }
            if (!slotData[idx].has_value()) {
                // （ここは実際は起きないはずだが安全策として Empty）
                return { PopStatus::Empty, std::nullopt };
            }

            // 5) 最後の１要素レース対応
            if (b1 == t0) {
                size_t expected = t0;
                // pop と steal のどちらが最後の 1 要素を取るか CAS で決める
                if (!top.compare_exchange_strong(
                    expected, t0 + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
                {
                    // steal 側が勝利 → bottom はそのまま、Empty 扱い
                    checkInvariant("POP LAST FAIL");
                    return { PopStatus::Empty, std::nullopt };
                }

                TaskPtr result = std::move(*slotData[idx]);
                slotData[idx].reset();               // ← reset は CAS 後
                bottom.store(t0+1, std::memory_order_seq_cst);
                checkInvariant("POP LAST OK");
                return { PopStatus::Success, std::move(result) };
            }

            TaskPtr result = std::move(*slotData[idx]);
            slotData[idx].reset();
            bottom.store(b1, std::memory_order_seq_cst);
            checkInvariant("POP NORMAL OK");
            return { PopStatus::Success, std::move(result) };
        }

        //―――――――――――――――――――――――――――――――――――
        // 他スレッドから stealTop
        StealResult stealTop() {
            // 1) empty 判定
            size_t t0 = top.load(std::memory_order_seq_cst);
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            if (t0 >= b0) {
                return { StealStatus::Empty, std::nullopt };
            }

            size_t idx = t0 & mask;

            // 2) 内部 tailMutex で一意制御 & スロットロック
            std::unique_lock tailLk(tailMutex, std::try_to_lock);
            if (!tailLk.owns_lock()) {
                return { StealStatus::WouldBlock, std::nullopt };
            }

            std::unique_lock lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock()) {
                return { StealStatus::WouldBlock, std::nullopt };
            }

            if (!slotData[idx].has_value()) {
                return { StealStatus::Empty, std::nullopt };
            }

            // 3) 要素を取り出し
            TaskPtr result = std::move(*slotData[idx]);
            slotData[idx].reset();

            // 4) 最後の 1 要素レース決着
            size_t expected = t0;
            if (!top.compare_exchange_strong(
                expected, t0 + 1,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst))
            {
                // pop 側に負けた → Empty
                checkInvariant("STEAL FAIL");
                return { StealStatus::Empty, std::nullopt };
            }

            checkInvariant("STEAL SUCCESS");
            return { StealStatus::Success, std::move(result) };
        }

        bool empty() const {
            size_t b = bottom;
            size_t t = top.load(std::memory_order_acquire);
            return t >= b;
        }

        //デバッグ用にのみ。
        size_t unsafe_size() const {
            return bottom - top.load();
        }

        void bugCheck() {
            std::vector<size_t>validSlots;

            bool isValidJob = false;
            for (size_t i = 0; i < capacity; i++)
            {
                if(slotData[i].has_value()){
                    isValidJob = true;
                    validSlots.push_back(i);
                }
            }

            if (validSlots.empty()) {
                return;
            }

            if(isValidJob){
                checkInvariant("BUGCHECK InfLoop");

                for (auto &index : validSlots)
                {
                    std::printf(
                        "[BUGCHECK] queue=%zu slotData[%2zu] : still has a pending job\n",
                        queueIndex, index);
                }
            }
        }

    private:
        void checkInvariant(const char* where) {
            size_t b = bottom.load(std::memory_order_relaxed);
            size_t t = top.load(std::memory_order_relaxed);

            std::printf("[%s] queue=%zu : bottom=%zu, top=%zu\n", where,queueIndex, b, t);
            ASSERT(b >= t,"deque invariant violated");
        }

        //void checkInvariants() {
        //    size_t t = top.load(std::memory_order_acquire);
        //    size_t b = bottom.load(std::memory_order_acquire);
        //    size_t cnt = count.load(std::memory_order_acquire);

        //    // 基本の不変条件
        //    ASSERT(t <= b,
        //        "Invariant failed: top > bottom"
        //        "  top=" << t
        //        << " bottom=" << b);

        //    ASSERT(b - t <= capacity,
        //        "Invariant failed: queue size exceeds capacity"
        //        "  size(b-t)=" << (b - t)
        //        << " capacity=" << capacity);

        //    ASSERT(cnt == b - t,
        //        "Invariant failed: element counter mismatch"
        //        " element count=" << cnt
        //        << " expected(b-t)=" << (b - t)
        //        << "  top=" << t
        //        << " bottom=" << b);
        //}

    private:

        std::vector<std::mutex>        slotMutex;
        std::mutex tailMutex;
        std::vector<std::optional<TaskPtr>>  slotData;

        // インデックス管理
        std::atomic<size_t> top;     // スティーラーが進める
        std::atomic<size_t> bottom;    // オーナーのみ
        const size_t      capacity;
        const size_t      mask;      // capacity は 2^N の前提

        const size_t queueIndex;
    };

}  // namespace ECS::JobSystem
