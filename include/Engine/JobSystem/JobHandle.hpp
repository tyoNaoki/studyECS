#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace ECS::JobSystem{

struct Job {

private:
    // 最大キャプチャ領域
    static constexpr size_t BufferSize = 32;

    // 呼び出し時の関数ポインタ型
    using Invoker = void(*)(void*);

    // 実データ格納＋呼び出し子
    alignas(void*) char  buf[BufferSize];
    Invoker              invoke_fn = nullptr;

public:
    Job() = default;

    Job(Job&& o) noexcept {
        invoke_fn = o.invoke_fn;
        memcpy(buf, o.buf, BufferSize);
        o.invoke_fn = nullptr;
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
    }

    // 一度きりの実行
    void invoke() noexcept {
        if (invoke_fn) {
            invoke_fn(buf);
            invoke_fn = nullptr;
        }
    }

    explicit operator bool() const noexcept {
        return invoke_fn != nullptr;
    }
};

struct Task {
    Job job;
    std::atomic<int>   in_degree{ 0 };
    Task* nextDependent;
    std::mutex taskMutex;
};

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

// 待ち手（読み取り専用ハンドル）
template<typename T>
class JobFuture {
    std::shared_ptr<FutureInner<T>> inner;
public:
    explicit JobFuture(std::shared_ptr<FutureInner<T>> i)
        : inner(std::move(i)) {}

    //JobSystem::run_one_pending_job() を呼びつつ待ち
    T get() {
        while (true) {
            // まず mutex を獲得して ready フラグをチェック
            {
                std::lock_guard lk(inner->mtx);
                if (inner->ready) {
                    if constexpr (!std::is_void_v<T>) {
                        return std::move(*inner->result);
                    }
                    else {
                        return;
                    }
                }
            }

            // まだ ready でなければ他ジョブをひとつ消化
            jobSystem.run_one_pending_job();
        }
    }
};

// 書き込み手（セット専用ハンドル）
template<typename T>
class SettableJobFuture {
    std::shared_ptr<FutureInner<T>> inner;
public:
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

using JobHandle = Task*;

};//namespace ECS::JobSystem