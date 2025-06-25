#pragma once

#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "HashFunctions.hpp"
#include "Debug.h"

namespace ECS{

constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

//一般型用のSparseSet実装
template<typename T, bool IsEmpty = std::is_empty_v<T>>
class SparseSetImpl;

template<typename T>
using SparseSet = SparseSetImpl<T>;

template<typename T>
class SparseSet_iterator {
    const std::vector<T>* ptr;  // underlying container
    size_t                 off; // 末尾からのオフセット＋1

public:
    using value_type = T;
    using reference = T&;
    using pointer = T*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    constexpr SparseSet_iterator() noexcept : ptr(nullptr), off(0) {}
    constexpr SparseSet_iterator(const std::vector<T>& v, size_t idx) noexcept
        : ptr(&v), off(idx + 1) {}

    // ++ で先頭方向へ
    constexpr SparseSet_iterator& operator++() noexcept { --off; return *this; }
    constexpr SparseSet_iterator operator++(int) noexcept { vec_rev_it tmp = *this; ++* this; return tmp; }

    constexpr SparseSet_iterator operator+(const difference_type value) const noexcept {
        SparseSet_iterator copy = *this;
        return (copy += value);
    }

    // -- で末尾方向へ
    constexpr SparseSet_iterator& operator--() noexcept { ++off; return *this; }

    constexpr SparseSet_iterator& operator+=(const difference_type value) noexcept {
        offset -= value;
        return *this;
    }

    constexpr SparseSet_iterator& operator-=(const difference_type value) noexcept {
        (*this += -value);
    }

    constexpr SparseSet_iterator operator-(const difference_type value) const noexcept {
        return (*this + -value);
    }

    // 現在の要素
    constexpr reference operator*() const noexcept {
        return (*ptr)[off - 1];
    }
    constexpr pointer operator->() const noexcept {
        return &operator*();
    }

    // ランダムアクセス
    constexpr reference operator[](difference_type d) const noexcept {
        return (*ptr)[off - 1 - d];
    }

    constexpr pointer data() const noexcept {
        return ptr ? ptr->data() : nullptr;
    }

    constexpr size_t index() const noexcept {
        return off - 1;
    }
};

// ランタイムでのインターフェース
class ISparseSet {
public:
    using iterator = SparseSet_iterator<EntityID>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_iterator = iterator;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    virtual ~ISparseSet() = default;
    virtual void Delete(EntityID) = 0;
    virtual void Clear() = 0;
    virtual size_t Size() = 0;
    virtual bool ContainsEntity(EntityID) = 0;
    virtual std::vector<EntityID> GetEntityList() = 0;
    virtual EntityID GetEntity(std::size_t) const = 0;
    virtual size_t Index(EntityID) =  0;
    virtual ecs_map::id_type Hash()const = 0;

    virtual void swap_elements(const EntityID lhs,const EntityID rhs) = 0;

    virtual iterator begin() const noexcept = 0;
    virtual iterator end() const noexcept = 0;

    virtual const_iterator begin() const noexcept = 0;
    virtual const_iterator end() const noexcept = 0;

    virtual reverse_iterator rbegin() const noexcept = 0;
    virtual reverse_iterator rend() const noexcept = 0;

    virtual const_reverse_iterator rbegin() const noexcept = 0;
    virtual const_reverse_iterator crbegin() const noexcept = 0;
    virtual const_reverse_iterator rend() const noexcept = 0;
    virtual const_reverse_iterator crend() const noexcept = 0;

};

// 非空型用
template<typename T>
class SparseSetImpl<T, false>:public ISparseSet {
    static constexpr size_t SPARSE_MAX_SIZE = 2048;

    using Sparse = std::array<size_t,SPARSE_MAX_SIZE>;
    using pointer = typename packed_container_type::const_pointer;

    static constexpr bool enable_hole_deletion =
        // ムーブ構築できない ＋ ムーブ代入できない 型 はホール削除
        (!std::is_move_constructible_v<T>)
        || (!std::is_move_assignable_v<T>);

    /*
    template<typename Type, typename = void>
    struct enable_hole_deletion
        : std::bool_constant<
        !(std::is_move_constructible_v<Type>
            && std::is_move_assignable_v<Type>)
        > {};

    // ユーザーが Type::enable_hole_deletion = true と書いた型は必ず true
    template<typename Type>
    struct enable_hole_deletion<Type,
        std::enable_if_t<Type::enable_hole_deletion>>
        : std::true_type {};

    // void 特殊化は常に false
    template<> struct enable_hole_deletion<void> : std::false_type {};
    */

public:
    virtual ~SparseSetImpl(){}

    using type = T;

    template<typename... Args>
    T* Emplace(EntityID entity, Args&&... args);

    template<typename... Args>
    T* Update(EntityID entity, Args&&... args);

    // エンティティに対応するコンポーネントを取得する
    T* Get(EntityID entity);

    // 参照を返す
    T& GetRef(EntityID entity);

    size_t Index(EntityID entity)override;

    ecs_map::id_type Hash() const override;

    void swap_elements(const EntityID lhs, const EntityID rhs) override;

    // 指定エンティティのコンポーネントを削除する
    virtual void Delete(EntityID entity) override;

    //内部のTオブジェをすべて削除
    void Clear() override;

    //現在のdense配列の大きさ取得
    size_t Size() override;

    EntityID GetEntity(std::size_t position) const override;

    //登録されているTオブジェ所持のEntityを配列で返す
    std::vector<EntityID> GetEntityList() override;

    //対象のEntityがTオブジェを所持しているか
    bool ContainsEntity(EntityID entity) override;

    //対象のEntityのdenseIndexを取得
    inline size_t GetDenseIndex(EntityID entity);
    //Sparse配列に設定
    inline void SetSparseIndex(EntityID entity,size_t index);
    
    //空チェック
    bool IsEmpty() const{
        return m_dense.empty();
    }

    std::tuple<T&> GetRef_as_tuple(EntityID entt)noexcept {
        return std::forward_as_tuple(GetRef(entt));
    }

    std::tuple<const T&> GetRef_as_tuple(const EntityID entt)const noexcept{
        return std::forward_as_tuple(GetRef(entt));
    }

    std::vector<EntityID>::const_pointer data() const noexcept {
        return m_denseToEntity.data();
    }

    iterator begin() const noexcept override {
        const auto pos = m_denseToEntity.size();
        return iterator{m_denseToEntity,pos}; 
    }

    iterator end() const noexcept override {
        return iterator{ m_denseToEntity,{}};
    }

    const_iterator begin() const noexcept override { return begin(); }
    const_iterator end() const noexcept override { return end(); }

    reverse_iterator rbegin() const noexcept override { return m_denseToEntity.rbegin(); }
    reverse_iterator rend() const noexcept override { return m_denseToEntity.rend(); }

    const_reverse_iterator rbegin() const noexcept override { std::make_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const noexcept override {
        return rbegin();
    }
    const_reverse_iterator rend() const noexcept override { std::make_reverse_iterator(begin()); }

    const_reverse_iterator crend() const noexcept override {
        return rend();
    }

private:
    std::vector<Sparse> m_sparsePages; // 疎テーブル
    std::vector<T> m_dense; // 密テーブル
    std::vector<EntityID>m_denseToEntity;
    std::atomic<uint32_t> n_reserved;
};

template<typename T>
template<typename ...Args>
inline T* SparseSetImpl<T, false>::Emplace(EntityID entity, Args && ...args)
{
    if (entity == INVALID_ENTITY) return nullptr;

    // 新規追加
    const size_t newIndex = m_dense.size();
    m_dense.emplace_back(std::forward<Args>(args)...);
    m_denseToEntity.push_back(entity);
    SetSparseIndex(entity, newIndex);
    return &m_dense.back();
}

template<typename T>
template<typename ...Args>
inline T* SparseSetImpl<T, false>::Update(EntityID entity, Args && ...args)
{
    if (entity == INVALID_ENTITY) return nullptr;

    auto idx = GetDenseIndex(entity);

    ASSERT(idx != NULL_INDEX,entity <<" do not have " << typeid(T).name());

    // 既存 object を再構築 or assign したければ
    m_dense[idx] = T{ std::forward<Args>(args)... };
    return &m_dense[idx];
}

template<typename T>
inline T* SparseSetImpl<T, false>::Get(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    return (index != NULL_INDEX)&&m_denseToEntity[index] == entity ? &m_dense[index] : nullptr;
}

template<typename T>
inline T& SparseSetImpl<T, false>::GetRef(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    if (index == NULL_INDEX)
        ASSERT(false,"GetRef called on invalid entity with " << EntityInfo(entity));

    return m_dense[index];
}

template<typename T>
inline size_t SparseSetImpl<T, false>::Index(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    if(index != NULL_INDEX && m_denseToEntity[index] == entity) return index;

    return NULL_INDEX;
}

template<typename T>
inline ecs_map::id_type SparseSetImpl<T, false>::Hash() const
{
    return ecs_map::type_hash<T>();
}

template<typename T>
inline void SparseSetImpl<T, false>::swap_elements(const EntityID lhs, const EntityID rhs)
{
    if (lhs == INVALID_ENTITY || rhs == INVALID_ENTITY) {
        std::cout<<"INVALID ENTITY"<<std::endl;
        return;
    }

    const auto fromIdx = Index(lhs);
    const auto toIdx = Index(rhs);

    //どちらかが無効なら何もしない
    if (fromIdx == NULL_INDEX || toIdx == NULL_INDEX) {
        std::cout << "NULL INDEX" << std::endl;
        return;
    }

    // ムーブ不可能型（ピン留め型）はサポート外
    static constexpr bool is_pinned_type =
        !(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>);
    ASSERT(!is_pinned_type, "Pinned type");

    //交換対象の Entity をキャッシュ
    const auto entFrom = lhs;
    const auto entTo = rhs;

    //dense交換
    std::swap(m_dense[fromIdx], m_dense[toIdx]);
    std::swap(m_denseToEntity[fromIdx], m_denseToEntity[toIdx]);

    //sparse交換
    SetSparseIndex(entFrom, toIdx);
    SetSparseIndex(entTo, fromIdx);
}

template<typename T>
inline void SparseSetImpl<T, false>::Delete(EntityID entity)
{
    if (!IsEntityValid(entity)) return;

    auto entityIndex = GetEntityIndex(entity);
    size_t deletedIndex = GetDenseIndex(entity);

    if (m_dense.empty() || deletedIndex == NULL_INDEX) return;

    SetSparseIndex(m_denseToEntity.back(), deletedIndex);
    SetSparseIndex(entity, NULL_INDEX);

    std::swap(m_dense.back(), m_dense[deletedIndex]);
    std::swap(m_denseToEntity.back(), m_denseToEntity[deletedIndex]);

    m_dense.pop_back();
    m_denseToEntity.pop_back();
}

template<typename T>
inline void SparseSetImpl<T, false>::Clear()
{
    m_sparsePages.clear();
    m_dense.clear();
    m_denseToEntity.clear();
}

template<typename T>
inline size_t SparseSetImpl<T, false>::Size()
{
    return m_dense.size();
}

template<typename T>
inline EntityID SparseSetImpl<T, false>::GetEntity(std::size_t position) const
{
    if(position < m_denseToEntity.size()) return m_denseToEntity[position];

    return INVALID_ENTITY;
}

template<typename T>
inline std::vector<EntityID> SparseSetImpl<T, false>::GetEntityList()
{
    return m_denseToEntity;
}

template<typename T>
inline bool SparseSetImpl<T, false>::ContainsEntity(EntityID entity)
{
    size_t index = GetDenseIndex(entity);

    return (index != NULL_INDEX && m_denseToEntity[index] == entity);
}

template<typename T>
inline size_t SparseSetImpl<T, false>::GetDenseIndex(EntityID entity)
{
    size_t index = GetEntityIndex(entity);
    size_t page = index / SPARSE_MAX_SIZE;
    size_t sparseIndex = index % SPARSE_MAX_SIZE;

    if (page < m_sparsePages.size()) {
        Sparse& sparse = m_sparsePages[page];
        return sparse[sparseIndex];
    }

    return NULL_INDEX;
}

template<typename T>
inline void SparseSetImpl<T, false>::SetSparseIndex(EntityID entity, size_t index)
{
    size_t entityIndex = GetEntityIndex(entity);
    size_t page = entityIndex / SPARSE_MAX_SIZE;
    size_t sparseIndex = entityIndex % SPARSE_MAX_SIZE; // Index local to a page

    if (page >= m_sparsePages.size()) {
        m_sparsePages.resize(page + 1);
        m_sparsePages[page].fill(NULL_INDEX);
    }

    Sparse& sparse = m_sparsePages[page];

    sparse[sparseIndex] = {index};
}

// 空型用
template<typename T>
class SparseSetImpl<T, true> :public ISparseSet{
    static constexpr size_t SPARSE_MAX_SIZE = 2048;

    using Sparse = std::array<size_t, SPARSE_MAX_SIZE>;

    static constexpr bool enable_hole_deletion =
        // ムーブ構築できない ＋ ムーブ代入できない 型 はホール削除
        (!std::is_move_constructible_v<T>)
        || (!std::is_move_assignable_v<T>);

public:
    template<typename... Args>
    T* Emplace(EntityID entity, Args&&... args){
        (void)sizeof...(args);

        if (entity == INVALID_ENTITY) return nullptr;

        // スパース／デンスにエンティティだけ追加
        SetSparseIndex(entity, m_denseToEntity.size());
        m_denseToEntity.push_back(entity);

        return nullptr;
    }

    template<typename... Args>
    T* Update(EntityID entity, Args&&... args) {
        return nullptr;
    };

    // エンティティに対応するコンポーネントを取得する
    T* Get(EntityID entity){return nullptr;};

    // 参照を返す
    T& GetRef(EntityID entity){
        ASSERT(false,"do not use Empty struct pool GetRef()!!");
        return T();
    };

    size_t Index(EntityID entity)override{
        size_t index = GetDenseIndex(entity);
        if (index != NULL_INDEX && m_denseToEntity[index] == entity) return index;

        return NULL_INDEX;
    };

    ecs_map::id_type Hash() const override{
        return ecs_map::type_hash<T>();
    };

    void swap_elements(const EntityID lhs, const EntityID rhs) override{
        if (lhs == INVALID_ENTITY || rhs == INVALID_ENTITY) {
            std::cout << "INVALID ENTITY" << std::endl;
            return;
        }

        const auto fromIdx = Index(lhs);
        const auto toIdx = Index(rhs);

        //どちらかが無効なら何もしない
        if (fromIdx == NULL_INDEX || toIdx == NULL_INDEX) {
            std::cout << "NULL INDEX" << std::endl;
            return;
        }

        //交換対象の Entity をキャッシュ
        const auto entFrom = lhs;
        const auto entTo = rhs;

        //dense交換
        std::swap(m_denseToEntity[fromIdx], m_denseToEntity[toIdx]);

        //sparse交換
        SetSparseIndex(entFrom, toIdx);
        SetSparseIndex(entTo, fromIdx);
    };

    // 指定エンティティのコンポーネントを削除する
    virtual void Delete(EntityID entity) override{
        if (!IsEntityValid(entity)) return;

        auto entityIndex = GetEntityIndex(entity);
        size_t deletedIndex = GetDenseIndex(entity);

        if (deletedIndex == NULL_INDEX) return;

        SetSparseIndex(m_denseToEntity.back(), deletedIndex);
        SetSparseIndex(entity, NULL_INDEX);

        std::swap(m_denseToEntity.back(), m_denseToEntity[deletedIndex]);

        m_denseToEntity.pop_back();

    };

    //内部のTオブジェをすべて削除
    void Clear() override{
        m_denseToEntity.clear();
        m_sparsePages.clear();
    };

    //現在のdense配列の大きさ取得
    size_t Size() override{
        return m_denseToEntity.size();
    };

    EntityID GetEntity(std::size_t position) const override{
        if (position < m_denseToEntity.size()) return m_denseToEntity[position];

        return INVALID_ENTITY;
    };

    //登録されているTオブジェ所持のEntityを配列で返す
    std::vector<EntityID> GetEntityList() override{
        return m_denseToEntity;
    };

    //対象のEntityがTオブジェを所持しているか
    bool ContainsEntity(EntityID entity) override{
        size_t index = GetDenseIndex(entity);

        return (index != NULL_INDEX && m_denseToEntity[index] == entity);
    };

    //対象のEntityのdenseIndexを取得
    inline size_t GetDenseIndex(EntityID entity){
        size_t index = GetEntityIndex(entity);
        size_t page = index / SPARSE_MAX_SIZE;
        size_t sparseIndex = index % SPARSE_MAX_SIZE;

        if (page < m_sparsePages.size()) {
            Sparse& sparse = m_sparsePages[page];
            return sparse[sparseIndex];
        }

        return NULL_INDEX;
    };

    //Sparse配列に設定
    inline void SetSparseIndex(EntityID entity, size_t index){
        size_t entityIndex = GetEntityIndex(entity);
        size_t page = entityIndex / SPARSE_MAX_SIZE;
        size_t sparseIndex = entityIndex % SPARSE_MAX_SIZE; // Index local to a page

        if (page >= m_sparsePages.size()) {
            m_sparsePages.resize(page + 1);
            m_sparsePages[page].fill(NULL_INDEX);
        }

        Sparse& sparse = m_sparsePages[page];

        sparse[sparseIndex] = { index };
    };

    //空チェック
    bool IsEmpty() const {
        return m_denseToEntity.empty();
    }

    std::tuple<> GetRef_as_tuple(EntityID entt)noexcept {
        return std::tuple{};
    }

    iterator begin() override { return m_denseToEntity.begin(); }
    iterator end() override { return m_denseToEntity.end(); }

    const_iterator begin() const override { return m_denseToEntity.begin(); }
    const_iterator end() const override { return m_denseToEntity.end(); }

    reverse_iterator rbegin() override { return m_denseToEntity.rbegin(); }
    reverse_iterator rend() override { return m_denseToEntity.rend(); }

    const_reverse_iterator rbegin() const override { return m_denseToEntity.rbegin(); }
    const_reverse_iterator rend() const override { return m_denseToEntity.rend(); }

private:
    std::vector<Sparse> m_sparsePages; // 疎テーブル
    std::vector<EntityID>m_denseToEntity;
    std::atomic<uint32_t> n_reserved;
};



}//namespace ECS
