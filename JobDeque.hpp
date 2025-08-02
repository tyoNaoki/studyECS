#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>
#include "taskPtr.hpp"
#include "JobManager.h"
#include "TestFramework.hpp"

namespace ECS::JobSystem {

    using TaskPtr = Ptr::intrusive_ptr<Task>;

    // pushBottom の結果
    enum class PushStatus {
        Success,    // 正常にpushできた
        WouldBlock, // ロック中で進めない
        Full        // バッファがすでに満杯
    };

    enum class PopStatus {
        Success,    // 正常にpopできた
        WouldBlock, // ロック中で進めない
        Empty        //空
    };

    enum class StealStatus {
        Success,    // 正常にstealできた
        WouldBlock, // ロック中で進めない
        Empty        //空
    };

    struct PushResult {
        PushStatus status;
        TaskPtr notPushed;
    };

    struct PopResult {
        PopStatus status;
        std::optional<TaskPtr> value;
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

        JobDeque(const JobDeque&) = delete;  //コピー禁止
        JobDeque& operator=(const JobDeque&) = delete;
        JobDeque(JobDeque&&) noexcept = default; //ムーブのみOK
        JobDeque& operator=(JobDeque&&) noexcept = default;

        //オーナースレッド専用：ボトムからPush
        PushResult pushBottom(TaskPtr job) {
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            size_t t0 = top.load(std::memory_order_seq_cst);

            if (b0 - t0 >= capacity) {
                return { PushStatus::Full, std::move(job) };
            }

            size_t idx = b0 & mask;

            //スロットロック＋中身チェック
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);

            if (!lk.owns_lock()) {
                return { PushStatus::WouldBlock, std::move(job) };
            }

            if (slotData[idx].has_value()) {
                return { PushStatus::Full, std::move(job) };
            }

            slotData[idx] = std::move(job);
            bottom.fetch_add(1,std::memory_order_release);

            const std::string log = "PUSH in Queue : " + getQueueIndex();
            checkInvariant(log.c_str());

            return { PushStatus::Success, {} };
        }

        //オーナースレッド専用：ボトムからPOP
        PopResult popBottom() {
            //empty判定
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            size_t t0 = top.load(std::memory_order_seq_cst);
            if (b0 <= t0) {
                return { PopStatus::Empty, std::nullopt };
            }

            //取り出し候補位置
            size_t b1 = b0 - 1;
            size_t idx = b1 & mask;

            //スロットロック＋中身チェック
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock()) {
                return { PopStatus::WouldBlock, std::nullopt };
            }
            if (!slotData[idx].has_value()) {
                return { PopStatus::Empty, std::nullopt };
            }

            //最後の１要素
            if (b1 == t0) {
                size_t curTop = top.load(std::memory_order_acquire);
                if (curTop != t0) {
                    bottom.store(curTop, std::memory_order_release);
                    return { PopStatus::Empty, std::nullopt };
                }

                size_t expected = t0;
                //popとstealのどちらが最後の1要素を取るか
                if (!top.compare_exchange_strong(
                    expected, t0 + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                {
                    //steal側が勝利 → bottomはそのままEmpty扱い
                    const std::string log = "POP LAST FAIL in Queue : " + std::to_string(getQueueIndex());
                    checkInvariant(log.c_str());
                    return { PopStatus::Empty, std::nullopt };
                }

                //基本この分岐を通る。
                TaskPtr result = std::move(*slotData[idx]);
                slotData[idx].reset();
                bottom.store(t0+1, std::memory_order_release);
                const std::string msg = "POP LAST OK in " + std::to_string(getQueueIndex());
                checkInvariant(msg.c_str());
                return { PopStatus::Success, std::move(result) };
            }

            TaskPtr result = std::move(*slotData[idx]);
            slotData[idx].reset();
            bottom.fetch_sub(1, std::memory_order_release);
            const std::string nlog = "POP NORMAL OK in Queue : " + std::to_string(getQueueIndex());
            checkInvariant(nlog.c_str());
            return { PopStatus::Success, std::move(result) };
        }

        //他スレッド専用：Topからsteal
        StealResult stealTop(size_t index) {

            //empty判定
            size_t t0 = top.load(std::memory_order_seq_cst);
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            if (t0 >= b0) {
                return { StealStatus::Empty, std::nullopt };
            }

            size_t idx = t0 & mask;

            //ロック、中身チェック
            //念のため、steal専用のロックも行う
            std::unique_lock tailLk(stealMutex, std::try_to_lock);
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

            //最後の1要素
            size_t expected = t0;
            if (!top.compare_exchange_strong(
                expected, t0 + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                //Steal失敗
                const std::string log = "STEAL FAIL of Queue : " +  std::to_string(index);
                checkInvariant(log.c_str());
                return { StealStatus::Empty, std::nullopt };
            }

            //基本、この分岐を通る。
            TaskPtr result = std::move(*slotData[idx]);
            //対象のスロットリセット
            slotData[idx].reset();

            const std::string slog = "STEAL SUCCESS of Queue : " + std::to_string(index);
            checkInvariant(slog.c_str());
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

        //Jobがまだスロット内に残っているかチェックし、ある場合ログとして出力
        bool validCheck() {
            std::vector<size_t>validSlots;

            for (size_t i = 0; i < capacity; i++)
            {
                if(slotData[i].has_value()){
                    validSlots.push_back(i);
                }
            }

            if (validSlots.empty()) {
                return false;
            }

            checkInvariant("JOB VALID CHECK");

            for (auto index : validSlots)
            {
                test::saveLog(
                    "[JOB VALID CHECK] queue=%zu slotData[%zu] : still has a pending job",
                    getQueueIndex(), index);
                std::printf(
                    "[JOB VALID CHECK] queue=%zu slotData[%zu] : still has a pending job\n",
                    getQueueIndex(), index);
            }

            return true;
        }

        size_t getQueueIndex(){return queueIndex;}

        //スタックなどの処理不可になった場合の緊急停止処置
        bool isAbort() const noexcept { return abortFlag;}

        //緊急停止設定時、ログも出力
        void setAbort(){
            if(abortFlag) return;

            abortFlag = true;
            test::saveLog(
                "[ABORT] queue=%zu",
                getQueueIndex());

            std::printf("[ABORT] queue=%zu\n",
                getQueueIndex());
        }

    private:

        //動作をログで保存
        void checkInvariant(const char* where) {
            size_t b = bottom.load(std::memory_order_seq_cst);
            size_t t = top.load(std::memory_order_seq_cst);

            test::saveLog("[%s] queue=%zu : bottom=%zu, top=%zu", where,queueIndex, b, t);
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
        std::mutex stealMutex;
        std::vector<std::optional<TaskPtr>>  slotData;

        // インデックス管理
        std::atomic<size_t> top;     // スティーラーが進める
        std::atomic<size_t> bottom;    // オーナーのみ
        const size_t      capacity;
        const size_t      mask;      // capacity は 2^N の前提

        const size_t queueIndex;

        bool abortFlag = false;
    };

}  // namespace ECS::JobSystem
