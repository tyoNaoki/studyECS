#pragma once
#include <atomic>
#include <new>
#include <type_traits>
#include <optional>
#include "TestFramework.hpp"

namespace ECS::JobSystem {

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

    class JobDeque {

    public:
        using PushResult = std::pair<PushStatus, std::optional<ChunkMeta>>;

        using PopResult = std::pair<PopStatus, std::optional<ChunkMeta>>;

        using StealResult = std::pair<StealStatus, std::optional<ChunkMeta>>;

        explicit JobDeque(
            size_t cap,
            size_t index)
            : top(0),
            bottom(0),
            capacity(cap),
            capMask(cap - 1),
            slotMutex(cap),
            slotData(cap),
            queueIndex(index)
        {
            ASSERT(capacity > 0 && (capacity & (capacity - 1)) == 0,
                "slotCapacity must be greater than zero and a power of two");
        }

        ~JobDeque() = default;

        JobDeque(const JobDeque&) = delete;  //コピー禁止
        JobDeque& operator=(const JobDeque&) = delete;
        JobDeque(JobDeque&&) noexcept = default; //ムーブのみOK
        JobDeque& operator=(JobDeque&&) noexcept = default;

        //slot残り
        size_t emptyNum() const {
            size_t curSize = size();
            return curSize < capacity ? capacity - curSize : 0;
        }

        bool empty() const {
            return top.load(std::memory_order_acquire)
                >= bottom.load(std::memory_order_relaxed);
        }

        size_t size() const {
            return bottom.load(std::memory_order_relaxed)
                - top.load(std::memory_order_relaxed);
        }

        size_t getQueueIndex() const { return queueIndex; }

        template<typename TaskQ>
        bool pushWithTimeout(ChunkMeta&& chunk,TaskQ&taskQ,std::chrono::milliseconds timeout = std::chrono::milliseconds{ 2 });

        template<typename JobQueue>
        bool popOrSteal(JobQueue* stealQueues,size_t stealQueueSize,ChunkMeta& chunk);

        PopStatus pop(ChunkMeta& chunk);

        template<typename JobQueue>
        StealStatus steal(JobQueue* queues,size_t stealQueueSize, ChunkMeta& chunk);

        // オーナースレッド専用：ボトムからPush
        PushResult pushBottom(ChunkMeta&& chunk) {
            size_t b0 = bottom.load(std::memory_order_seq_cst);
            size_t t0 = top.load(std::memory_order_seq_cst);

            //slot切れ
            if (b0 - t0 >= capacity) {
                return { PushStatus::Full, std::move(chunk) };
            }

            size_t idx = b0 & capMask;
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);

            if (!lk.owns_lock() || slotData[idx].has_value()) {
                return { PushStatus::WouldBlock, std::move(chunk)};
            }

            // 実際にチャンクを詰める
            slotData[idx] = std::move(chunk);
            bottom.fetch_add(1, std::memory_order_release);

            checkInvariant(("PUSH OK in Queue:" + std::to_string(queueIndex)).c_str());
            return { PushStatus::Success,std::nullopt };
        }

        //Jobがまだスロット内に残っているかチェックし、ある場合ログとして出力
        bool validCheck() {
            std::vector<size_t>validSlots;

            for (size_t i = 0; i < capacity; i++)
            {
                if(slotData[i]){
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

            size_t idx = b1 & capMask;

            //スロットロック＋中身チェック
            std::unique_lock lk(slotMutex[idx], std::try_to_lock);
            if (!lk.owns_lock()) {
                return { PopStatus::WouldBlock, std::nullopt };
            }
            if (!slotData[idx].has_value()) {
                return { PopStatus::Empty, std::nullopt};
            }

            //最後の１要素
            if (b1 == t0) {
                size_t curTop = top.load(std::memory_order_acquire);

                if (curTop != t0) {
                    bottom.store(curTop, std::memory_order_release);
                    return { PopStatus::Empty,std::nullopt};
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
                    return { PopStatus::Empty, std::nullopt};
                }

                //基本この分岐を通る。
                ChunkMeta result = std::move(*slotData[idx]);
                slotData[idx] = std::nullopt;
                bottom.store(t0 + 1, std::memory_order_release);
                const std::string msg = "POP LAST OK in " + std::to_string(getQueueIndex());
                checkInvariant(msg.c_str());
                return { PopStatus::Success, std::move(result) };
            }

            ChunkMeta result = std::move(*slotData[idx]);
            slotData[idx] = std::nullopt;

            bottom.fetch_sub(1, std::memory_order_release);

            //ログ
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
                return { StealStatus::Empty, ChunkMeta()};
            }

            size_t idx = t0 & capMask;

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

            if (!slotData[idx]) {
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
                const std::string log = "STEAL FAIL of Queue : " + std::to_string(index);
                checkInvariant(log.c_str());
                return { StealStatus::Empty, std::nullopt };
            }

            //基本、この分岐を通る。
            ChunkMeta result = std::move(*slotData[idx]);
            //対象のスロットリセット
            slotData[idx] = std::nullopt;

            const std::string slog = "STEAL SUCCESS of Queue : " + std::to_string(index);
            checkInvariant(slog.c_str());

            return { StealStatus::Success, std::move(result) };
        }

    private:

        //動作をログで保存
        void checkInvariant(const char* where) {
            size_t b = bottom.load(std::memory_order_seq_cst);
            size_t t = top.load(std::memory_order_seq_cst);

            test::saveLog("[%s] queue=%zu : bottom=%zu, top=%zu", where,queueIndex, b, t);
            ASSERT(b >= t,"deque invariant violated");
        }

    private:
        const size_t capacity;
        const size_t capMask;
        std::vector<std::mutex>      slotMutex;
        std::vector<std::optional<ChunkMeta>> slotData;

        // インデックス & atomic カウンタ
        const size_t         queueIndex;
        std::atomic<size_t>  top;
        std::atomic<size_t>  bottom;

        std::mutex stealMutex;

        bool abortFlag = false;
    };

    inline PopStatus JobDeque::pop(ChunkMeta& chunk)
    {
        auto popRes = popBottom();

        if (popRes.first == PopStatus::Success) {
            chunk = std::move(*popRes.second);
            return PopStatus::Success;
        }
        else if (popRes.first == PopStatus::WouldBlock) {
            return PopStatus::WouldBlock;
        }

        return PopStatus::Empty;
    }

    template<typename TaskQ>
    inline bool JobDeque::pushWithTimeout(ChunkMeta&& chunk, TaskQ& taskQ, std::chrono::milliseconds timeout)
    {
        auto start = std::chrono::steady_clock::now(); 

        auto c = std::move(chunk);

        while (true) {
            auto [status, notPushed] = pushBottom(std::move(c));

            if(status == PushStatus::Success){
                return true;
            }

            //タイムアウト
            //元のタスクキューに返す
            if (std::chrono::steady_clock::now() - start >= timeout) {
                taskQ.enqueue(std::move(*notPushed));
                //ASSERT(false,"not work");
                return false;
            }

            //少し待って再挑戦
            c = std::move(*notPushed);
            std::this_thread::yield();
        }

        return false;
    }

    template<typename JobQueue>
    inline bool JobDeque::popOrSteal(JobQueue* stealQueues,size_t stealQueueSize,ChunkMeta &chunk)
    {
        //自キューからPOP
        {
            auto popRes = pop(chunk);
        
            if (popRes == PopStatus::Success) {
                return true;
            }
            else if (popRes == PopStatus::WouldBlock) {
                std::this_thread::yield();
                return true;
            }
        }
        
        //他キューからSteal
        {
            auto result = steal(stealQueues,stealQueueSize,chunk);
            if (result == StealStatus::Success) {
                return true;
            }
        }
        
        return false;
    }

    template<typename JobQueue>
    inline StealStatus JobDeque::steal(JobQueue* queues,size_t stealQueueSize, ChunkMeta &chunk)
    {
        //size_t n = (*queues)->size();
        size_t n = stealQueueSize;

        StealResult result;
        for (size_t i = 1; i < n; ++i) {
            size_t idx = (queueIndex + i) % n;
            result = queues[idx]->stealTop(queueIndex);

            if (result.first == StealStatus::Success) {
                chunk = std::move(*result.second);
                return StealStatus::Success;
            }
        }

        return StealStatus::Empty;
    }

}  // namespace ECS::JobSystem
