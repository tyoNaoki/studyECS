#pragma once
#include <memory>
#include <vector>
#include <iostream>
#include <cstdint>
#include "HopscotchHashMap.h"

namespace ECS{
namespace ecs_map {

    template<typename Key, typename Value>
    class SparseHopscotchHashMap {
        std::vector<std::pair<Key,size_t>>sparse;
        std::vector<Value> dense;
        std::vector<size_t>denseToSparse;
        std::vector<uint8_t> hopscotchBitmap;
        std::size_t capacity;

    public:
        auto begin() { return dense.begin(); }
        auto end() { return dense.end(); }

        //capacityを定数指定()
        SparseHopscotchHashMap() : capacity(minimum_capacity) {
            sparse.resize(capacity);
            hopscotchBitmap.resize(capacity, 0);
        }

        //capacity指定
        SparseHopscotchHashMap(std::size_t cap) : capacity(cap) {
            sparse.resize(cap);
            hopscotchBitmap.resize(cap, 0);
        }

        void insert(const Key& key, const Value& value) {
            std::size_t index = FNV1aHash(key) % capacity;
            while (hopscotchBitmap[index] & (1 << (index % 8))) {
                index = (index + 1) % capacity;
            }
            size_t backIndex = dense.size();
            dense.push_back(value);
            denseToSparse.push_back(index);
            sparse[index] = {key, backIndex };
            hopscotchBitmap[index] |= (1 << (index % 8));  // 局所性確保
        }



        Value* find(const Key& key) {
            std::size_t index = FNV1aHash(key) % capacity;

            while (hopscotchBitmap[index] & (1 << (index % 8)) && sparse[index].first != key) {
                index = (index + 1) % capacity;
            }

            return (hopscotchBitmap[index] & (1 << (index % 8))) ? &dense[sparse[index].second] : nullptr;
        }

        bool contains(const Key& key) {
            std::size_t index = FNV1aHash(key) % capacity;

            while (hopscotchBitmap[index] & (1 << (index % 8)) && sparse[index].first != key) {
                index = (index + 1) % capacity;
            }

            return (hopscotchBitmap[index] & (1 << (index % 8)));
        }

        template<typename Func>
        void each(Func func) const {
            for (std::size_t i = 0; i < capacity; ++i) {
                if (hopscotchBitmap[i] & (1 << (i % 8))) {  // バケットが有効なら処理
                    func(dense[sparse[i].second]);  // 値 (value) のみ関数に適用
                }
            }
        }

        bool erase(const Key& key) {
            std::size_t index = FNV1aHash(key) % capacity;

            // 該当キーを探索
            while (hopscotchBitmap[index] & (1 << (index % 8)) && sparse[index].first != key) {
                index = (index + 1) % capacity;
            }

            // キーが見つかった場合
            if (hopscotchBitmap[index] & (1 << (index % 8))) {
                hopscotchBitmap[index] &= ~(1 << (index % 8)); // ビットをリセット
                
                size_t lastIndex = dense.size()-1;
                size_t deleteIndex = sparse[index].second;
                //sparse情報更新
                sparse[index] = {};
                auto backKey = sparse[denseToSparse.back()].first;
                sparse[denseToSparse.back()] = { backKey,deleteIndex };

                std::swap(dense.back(),dense[deleteIndex]);
                std::swap(denseToSparse.back(),denseToSparse[deleteIndex]);

                dense.pop_back();
                denseToSparse.pop_back();
                return true;
            }

            return false;
        }

        std::size_t size() const {
            std::size_t count = 0;
            for (std::size_t i = 0; i < capacity; ++i) {
                if (hopscotchBitmap[i] & (1 << (i % 8))) {
                    count++;
                }
            }
            return count;
        }

        void clear() {
            for (std::size_t i = 0; i < capacity; ++i) {
                hopscotchBitmap[i] = 0;  // 全てのビットをリセット
                sparse[i] = {};  // データを削除
            }
            dense.clear();
            denseToSparse.clear();
        }
    };

}//namespace ecs_map
}//namespace ECS
