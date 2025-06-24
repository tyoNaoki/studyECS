#pragma once
#include "SparseSet.h"
#include <memory>
#include <vector>
#include "Debug.h"
#include <iostream>
#include <cstdint>
#include "HashFunctions.hpp"

namespace ECS{
namespace ecs_map {

// Hopscotch HashMap の設定
static constexpr std::size_t minimum_capacity = 8u;   // 初期バケットサイズ
static constexpr float default_threshold = 0.875f;    // リハッシュの閾値
static constexpr std::size_t max_hop_distance = 16;   // ホップスコッチの最大検索距離
static constexpr std::size_t rehash_factor = 1.5;       // リサイズ時の拡張率

template<typename,typename>
class HopscotchHashMap;

template<bool IsConst,typename Key,typename Value>
class HopScotch_iterator {
    template <bool B,typename K, typename V>
    friend class HopScotch_iterator;

    using size_type = std::size_t;
    using map_t = std::conditional_t<IsConst, const HopscotchHashMap<Key, Value>, HopscotchHashMap<Key, Value>>; 
    using value_t = std::conditional_t<IsConst, const Value, Value>;  

    using iterator = size_type;

    const HopscotchHashMap<Key,Value>* map_;
    size_type pos_;

public:
    HopScotch_iterator(const HopscotchHashMap<Key,Value>* map, size_type pos) noexcept
        : map_(map), pos_(pos) {}

    HopScotch_iterator& operator++() noexcept {
        pos_ = map_->next(pos_);
        return *this;
    }

    bool operator!=(const HopScotch_iterator& other) const noexcept{
        return pos_ != other.pos_;
    }

    const Value& operator*() const noexcept{
        return map_->getValue(pos_);
    }

    template<bool B>
    bool operator!=(const HopScotch_iterator<B, Key, Value>& other) const noexcept {
        return pos_ != other.pos_;
    }
};

template<typename Key, typename Value>
class HopscotchHashMap{
    static_assert(std::is_integral_v<Key>, "Key must be an integer type (e.g., int, uint32_t, uint64_t)");

    using iterator = HopScotch_iterator<false,Key, Value>;
    using const_iterator = HopScotch_iterator<true,Key, Value>;

    std::vector<std::pair<Key, Value>> data;
    std::vector<uint16_t> hopscotch_Bitmap_dist;
    std::size_t capacity;
    uint8_t max_dist;

private:
    inline constexpr uint8_t dist_from_Bitmap(uint16_t bitmap) noexcept {
        return static_cast<uint8_t>(bitmap & 0xFF00) >> 8;
    }

    inline constexpr uint8_t calculate_max_dist(std::size_t capacity) noexcept {
        return static_cast<uint8_t>(std::log2(capacity)); 
    }

    constexpr size_t find_first_valid(size_t pos) const noexcept {
        while (pos < capacity) {
            if (hopscotch_Bitmap_dist[pos] != 0) {  // すでに占有されているなら、位置をそのまま返す
                return pos;
            }
            pos++;
        }
        return pos;
    }

public:
    //capacityを定数指定()
    HopscotchHashMap() : capacity(minimum_capacity), max_dist(calculate_max_dist(minimum_capacity)) {
        data.resize(capacity);
        hopscotch_Bitmap_dist.resize(capacity, 0);
    }

    //capacity指定
    HopscotchHashMap(std::size_t cap) : capacity(cap), max_dist(calculate_max_dist(cap)) {
        data.resize(cap);
        hopscotch_Bitmap_dist.resize(cap, 0);
    }

    auto begin() noexcept -> iterator {
        return iterator(this, find_first_valid(0));
    }

    auto cbegin() const noexcept -> const_iterator {
        return const_iterator(this, find_first_valid(0));
    }

    auto end() noexcept -> iterator {
        return iterator(this, find_first_valid(capacity));
    }

    auto cend() const noexcept -> const_iterator {
        return const_iterator(this, find_first_valid(capacity));
    }

    size_t next(size_t pos) const noexcept{
        return find_first_valid(pos + 1);
    }

    const Value& getValue(const size_t pos) const
    {
        return data[pos].second;
    }

    void insert(const Key& key, const Value& value) {
        std::size_t index = key % capacity;
        uint8_t dist = 0;

        while (hopscotch_Bitmap_dist[index] &0xFF) {
            index = (index + 1) % capacity;

            // 上限回数を設定し、限界を超えたらリサイズ
            if (++dist > max_dist) {
                rehash();
                index = key % capacity;  
                dist = 0;
            }
        }

        data[index] = { key, value };
        hopscotch_Bitmap_dist[index] =((dist << 8)) | (1 << (index % 8));
    }

    void rehash() {
        size_t newCapacity = capacity * 2;

        std::vector<std::pair<Key, Value>> newData(newCapacity);
        std::vector<uint16_t> newBitmap(newCapacity, 0);

        for (size_t i = 0; i < capacity; ++i) {
            if (hopscotch_Bitmap_dist[i]) {
                size_t newIndex = data[i].first % newCapacity;
                newData[newIndex] = std::move(data[i]);  
                newBitmap[newIndex] = hopscotch_Bitmap_dist[i];
            }
        }

        data = std::move(newData);
        hopscotch_Bitmap_dist = std::move(newBitmap);
        capacity = newCapacity;
        max_dist = calculate_max_dist(newCapacity);
    }

    Value* find(const Key& key) {
        std::size_t index = key % capacity;
        auto dist = dist_from_Bitmap(hopscotch_Bitmap_dist[index]);

        while (dist <= max_dist) {
            if (!(hopscotch_Bitmap_dist[index] & 0xFF)) return nullptr;

            //Key一致
            if (data[index].first == key) return &data[index].second;

            index = (index + 1) % capacity;
            dist = hopscotch_Bitmap_dist[index] >> 8;
        }

        return  nullptr;
    }

    inline bool contains(const Key& key){
        std::size_t index = key % capacity;
        auto dist = dist_from_Bitmap(hopscotch_Bitmap_dist[index]);

        while (dist <= max_dist) {
            if (!(hopscotch_Bitmap_dist[index] & 0xFF)) return false;

            //Key一致
            if (data[index].first == key) return true;

            index = (index + 1) % capacity;
            dist = hopscotch_Bitmap_dist[index] >> 8;
        }

        return false;
    }

    template<typename Func>
    inline void each(Func func) const {
        for (std::size_t i = 0; i < capacity; ++i) {
            uint8_t bitmask = 1 << (i % 8);
            if (hopscotch_Bitmap_dist[i] & bitmask) {
                func(data[i].second);
            }
        }
    }

    inline bool erase(const Key&key){
        std::size_t index = key % capacity;

        // 該当キーを探索
        while (hopscotch_Bitmap_dist[index] & (1 << (index % 8)) && data[index].first != key) {
            index = (index + 1) % capacity;
        }

        // キーが見つかった場合
        if (hopscotch_Bitmap_dist[index] & (1 << (index % 8))) {
            hopscotch_Bitmap_dist[index] &= ~(1 << (index % 8)); // ビットをリセット
            data[index] = {}; // データを削除
            return true;
        }

        return false;
    }

    std::size_t size() const {
        return std::count_if(hopscotch_Bitmap_dist.begin(), hopscotch_Bitmap_dist.end(),
            [](uint16_t bitmap) { return bitmap & 0xFF; });
    }

    void clear() {
        for (std::size_t i = 0; i < capacity; ++i) {
            hopscotch_Bitmap_dist[i] = 0;  // リセット
            data[i] = {};  // データを削除
        }
    }
private:
    /*
    size_t find_first_valid(size_t pos) const {
        
        while (pos < capacity && !(hopscotchBitmap[pos] & (1 << (pos % 8)))) {
            pos++;
        }
        return pos;
    }
    */
    
};

namespace test {
    inline void testInsertAndRetrieve() {
        HopscotchHashMap<EntityIndex, std::string> map;
        //map.insert(1, "hello");
        //auto result = map.find(1);
        //assertEquals("hello"!=*result,"hello!=result"); // ない場合は空文字と比較
    }

    inline void testKeyNotFound() {
        HopscotchHashMap<int, std::string> map;
        //assertEquals(nullptr!=map.find(99),"nullptr!=map.find(99)"); // 存在しないキーは null を返す
    }

    inline void testErase() {
        HopscotchHashMap<int, std::string> map;
       // map.insert(1, "hello");
        //map.erase(1);
       // assertEquals(nullptr!=map.find(1),"nullptr!=map.find(1)"); // 削除後は null
    }

    inline void testSizeAndClear() {
        HopscotchHashMap<int, std::string> map;
       // map.insert(1, "hello");
        //map.insert(2, "world");
       // assertEquals(2!=map.size(),"2!=map.size()");
       // map.clear();
        //assertEquals(0!=map.size(),"0!=map.size()");
    }
    
}//namespace test

}//namespace ecs_map
}//namespace ECS

