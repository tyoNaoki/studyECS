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
        // capacity は必ず 2 の累乗
        explicit ChaseLevDeque(size_t capacity)
            : capacity_(capacity),
            mask_(capacity - 1),
            buffer_(static_cast<Storage*>(
                operator new[](capacity * sizeof(Storage)))),
            top_(0),
            bottom_(0)
        {
            assert((capacity & (capacity - 1)) == 0 &&
                "Capacity must be a power of two");
        }

        ~ChaseLevDeque() {
            // ここでは live 要素の破棄を行いません。
            // デストラクタ呼び出し責任はユーザーに委ねるか、
            // clear() 等で明示的に行ってください。
            operator delete[](buffer_);
        }

        // ボトムにタスクをプッシュ（オーナースレッドのみ呼ぶ）
        bool pushBottom(T&& task) {
            size_t b = bottom_.load(std::memory_order_relaxed);
            size_t t = top_.load(std::memory_order_acquire);
            if (b - t >= capacity_)
                return false;  // オーバーフロー

            // 配置 new で要素をコンストラクト
            new (&buffer_[b & mask_]) T(std::move(task));

            // リリースフェンス→他スレッドに要素が見えるように
            std::atomic_thread_fence(std::memory_order_release);
            bottom_.store(b + 1, std::memory_order_relaxed);
            return true;
        }

        // ボトムからポップ（オーナースレッドのみ呼ぶ）
        // 何もなければ std::nullopt、あれば std::move で返す
        std::optional<T> popBottom() {
            //1) 空かどうかを先にチェック
            size_t b0 = bottom_.load(std::memory_order_relaxed);
            size_t t0 = top_.load(std::memory_order_acquire);
            if (t0 >= b0) {
                // 空なら何もしない
                return std::nullopt;
            }

            // 2) 安全にデクリメントして要素を取る
            size_t b = b0 - 1;
            bottom_.store(b, std::memory_order_relaxed);

            // 全スレッドと同期
            std::atomic_thread_fence(std::memory_order_seq_cst);
            size_t t = top_.load(std::memory_order_relaxed);

            // 再チェック：空になってしまったか？
            if (t > b) {
                // 他スレッドに奪われたり空になった場合は bottom を復帰
                bottom_.store(t, std::memory_order_relaxed);
                return std::nullopt;
            }

            // 3) 要素を取り出して破棄
            T* slot = reinterpret_cast<T*>(&buffer_[b & mask_]);
            T result = std::move(*slot);
            slot->~T();

            // 4) “最後の 1 要素” なら top も進める
            if (t == b) {
                size_t expected = t;
                if (!top_.compare_exchange_strong(
                    expected, t + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed))
                {
                    // すでに steal された→空扱い
                    bottom_.store(t + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                // bottom も一致させる
                bottom_.store(t + 1, std::memory_order_relaxed);
            }

            return result;
        }


        // トップからスティール（他スレッドが呼ぶ）
        std::optional<T> stealTop() {
            size_t t = top_.load(std::memory_order_acquire);

            // 強いフェンスで bottom と同期
            std::atomic_thread_fence(std::memory_order_seq_cst);
            size_t b = bottom_.load(std::memory_order_acquire);

            if (t >= b)
                return std::nullopt;  // 空

            // 要素取り出し
            T* slot = reinterpret_cast<T*>(&buffer_[t & mask_]);
            T result = std::move(*slot);

            // head を進める CAS
            if (top_.compare_exchange_strong(
                t, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed))
            {
                slot->~T();  // 破棄
                return result;
            }
            // CAS に失敗したら empty 扱い
            return std::nullopt;
        }

        bool empty() const {
            size_t t = top_.load(std::memory_order_acquire);
            size_t b = bottom_.load(std::memory_order_acquire);
            return t >= b;
        }

        // スレッドセーフではありません。デバッグ用にのみ。
        size_t unsafe_size() const {
            return bottom_.load() - top_.load();
        }

    private:
        using Storage = typename std::aligned_storage<
            sizeof(T), alignof(T)>::type;

        const size_t        capacity_;
        const size_t        mask_;
        Storage* buffer_;
        std::atomic<size_t> top_;
        std::atomic<size_t> bottom_;
    };

}  // namespace ECS::JobSystem
