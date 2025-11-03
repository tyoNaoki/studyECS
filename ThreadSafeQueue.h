#pragma once

#include <vector>
#include <mutex>
#include "TaskQueue.h"

namespace ECS::JobSystem{

class ThreadSafeQueue {
private:
    std::vector<ChunkMeta> queue;
    mutable std::mutex mutex;

public:
    // 要素の追加
    void push(ChunkMeta&& chunk) {
        std::lock_guard<std::mutex> lock(mutex);

        queue.push_back(std::move(chunk));
    }

    // 要素の取り出し（ブロッキング版）
    bool pop(std::vector<ChunkMeta>& chunks) {
        std::lock_guard<std::mutex> lock(mutex);

        if (queue.empty()) return false;

        chunks.swap(queue);
        return true;
    }

    // 空チェック
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    // サイズ取得
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
};
};//namespace ECS::JobSystem