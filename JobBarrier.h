#pragma once
#include <mutex>
#include <condition_variable>
#include <cstddef>

namespace ECS::JobSystem{

class JobBarrier {
public:
    JobBarrier() = default;

    // total: バリアを抜けるために全員揃うスレッド数
    JobBarrier(std::size_t total)
        : m_total(total), m_count(0), m_generation(0)
    {}

    JobBarrier(const JobBarrier&) = delete;
    JobBarrier& operator=(const JobBarrier&) = delete;
    JobBarrier(JobBarrier&&) = delete;
    JobBarrier& operator=(JobBarrier&&) = delete;

    // すべてのスレッドがこの wait() を呼ぶまでブロックし、
    // 最後の一人が呼んだ瞬間に全員を同時リリースする
    void wait() {
        std::unique_lock<std::mutex> lk(m_mutex);
        auto gen = m_generation;

        if (++m_count == m_total) {
            // 最後の一人：次の世代に進める
            m_generation++;
            m_count = 0;
            m_cv.notify_all();
        }
        else {
            // 他のスレッドが来るまで待機
            m_cv.wait(lk, [this, gen] {
                return gen != m_generation;
                });
        }
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::size_t             m_total;      // 参加するスレッド数
    std::size_t             m_count;      // 今何人来たか
    std::size_t             m_generation; // 世代（バリアが何回リセットされたか）
};

}//namespace ECS::JobSystem