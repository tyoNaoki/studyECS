#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T*> queue;
    mutable std::mutex mutex;
    std::condition_variable not_empty;

public:
    // 要素の追加
    void push(T* value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(value));
        not_empty.notify_one();
    }

    // 要素の取り出し（ブロッキング版）
    T* pop() {
        std::unique_lock<std::mutex> lock(mutex);
        not_empty.wait(lock, [this] { return !queue.empty(); });
        T* value = std::move(queue.front());
        queue.pop();
        return value;
    }

    // 要素の取り出し（非ブロッキング版）
    bool try_pop(T*& value) {
        std::lock_guard<std::mutex> lock(mutex);

        if (queue.empty()) {
            return false;
        }

        value = std::move(queue.front());
        queue.pop();
        return true;
    }

    bool try_manyPop(size_t maxPopCount,std::vector<T*>&values){
        std::lock_guard<std::mutex> lock(mutex);
        if(queue.empty()||maxPopCount <= 0){
            return false;
        }

        size_t popCount = std::min(maxPopCount, queue.size());
        values.reserve(popCount);

        for(size_t count = 0;count < popCount;count++){
            T* value = std::move(queue.front());
            queue.pop();
            values.push_back(value);

            if(queue.empty()) break;
        }

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