#pragma once

#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "HashFunctions.hpp"
#include "TestFramework.hpp"

namespace ECS{

constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

//一般型用のSparseSet実装
template<typename T, bool IsEmpty = std::is_empty_v<T>>
class SparseSetImpl;

template<typename T>
using SparseSet = SparseSetImpl<T>;

template<typename T>
struct SparseSet_iterator final{
    using value_type = T;
    using reference = const T&;
    using pointer = const T*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    constexpr SparseSet_iterator() noexcept : ptr(nullptr), off(0) {}
    constexpr SparseSet_iterator(const std::vector<T>& v, const size_t idx) noexcept
        : ptr(&v), off(idx) {}

    constexpr SparseSet_iterator& operator++() noexcept { --off; return *this; }
    constexpr SparseSet_iterator operator++(int) noexcept { SparseSet_iterator tmp = *this; ++* this; return tmp; }

    constexpr SparseSet_iterator operator+(const difference_type value) const noexcept {
        SparseSet_iterator copy = *this;
        return (copy += value);
    }

    constexpr SparseSet_iterator& operator--() noexcept { ++off; return *this; }
    constexpr SparseSet_iterator& operator+=(const difference_type value) noexcept {
        off -= value;
        return *this;
    }

    constexpr SparseSet_iterator& operator-=(const difference_type value) noexcept {
        (*this += -value);
    }

    constexpr SparseSet_iterator operator-(const difference_type value) const noexcept {
        return (*this + -value);
    }

    constexpr reference operator[](const difference_type d) const noexcept {
        return (*ptr)[off - 1 - d];
    }

    // 現在の要素
    constexpr reference operator*() const noexcept {
        return (*ptr)[off - 1];
    }

    constexpr pointer operator->() const noexcept {
        return &operator*();
    }

    constexpr pointer data() const noexcept {
        return ptr ? ptr->data() : nullptr;
    }

    constexpr difference_type index() const noexcept {
        return off - 1;
    }

    friend constexpr bool operator==(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return a.index() == b.index();
    }

    friend constexpr std::ptrdiff_t operator-(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return a.index() - b.index();
    }

    friend constexpr bool operator!=(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return !(a == b);
    }

    friend constexpr bool operator<(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return a.index() > b.index();
    }

    friend constexpr bool operator>(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return a < b;
    }

    friend constexpr bool operator<=(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return !(a > b);
    }

    friend constexpr bool operator>=(const SparseSet_iterator& a, const SparseSet_iterator& b) noexcept {
        return !(a < b);
    }

private:
    const std::vector<T>* ptr;  // underlying container
    difference_type                 off; // 末尾からのオフセット＋1
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
    virtual size_t Size() const noexcept= 0;
    virtual bool ContainsEntity(const EntityID) const noexcept= 0;
    virtual std::vector<EntityID>& GetEntityList() noexcept= 0;
    virtual EntityID GetEntity(const std::size_t) const = 0;
    virtual size_t Index(EntityID) const=  0;
    virtual ecs_map::id_type Hash()const = 0;

    virtual void swap_elements(const EntityID lhs,const EntityID rhs) = 0;

    virtual iterator begin() noexcept = 0;
    virtual iterator end() noexcept = 0;

    virtual const_iterator begin() const noexcept = 0;
    virtual const_iterator end() const noexcept = 0;

    virtual reverse_iterator rbegin() noexcept = 0;
    virtual reverse_iterator rend() noexcept = 0;

    virtual const_reverse_iterator crbegin() const noexcept = 0;
    virtual const_reverse_iterator crend() const noexcept = 0;
};

// 非空型用
template<typename T>
class SparseSetImpl<T, false>:public ISparseSet {
    static constexpr size_t SPARSE_MAX_SIZE = 2048;

    using Sparse = std::array<size_t,SPARSE_MAX_SIZE>;

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
    const T& GetRef(EntityID entity)const;

    T& GetRef(EntityID entity);

    size_t Index(const EntityID entity)const override;

    ecs_map::id_type Hash() const override;

    void swap_elements(const EntityID lhs, const EntityID rhs) override;

    // 指定エンティティのコンポーネントを削除する
    virtual void Delete(EntityID entity) override;

    //内部のTオブジェをすべて削除
    void Clear() override;

    //現在のdense配列の大きさ取得
    size_t Size() const noexcept override;

    EntityID GetEntity(const std::size_t position) const override;

    //登録されているTオブジェ所持のEntityを配列で返す
    std::vector<EntityID>& GetEntityList() noexcept override;

    //登録されているTオブジェ所持のEntityを配列で返す
    std::vector<T>& GetValues() noexcept;

    //対象のEntityがTオブジェを所持しているか
    bool ContainsEntity(const EntityID entity) const noexcept override;

    //対象のEntityのdenseIndexを取得
    inline size_t GetDenseIndex(const EntityID entity) const;
    //Sparse配列に設定
    inline void SetSparseIndex(const EntityID entity,const size_t index);
    
    //空チェック
    bool IsEmpty() const{
        return m_dense.empty();
    }

    std::tuple<const T&> GetRef_as_tuple(const EntityID entt)const noexcept {
        return std::forward_as_tuple(GetRef(entt));
    }

    std::tuple<T&> GetRef_as_tuple(EntityID entt)noexcept {
        return std::forward_as_tuple(GetRef(entt));
    }

    std::vector<EntityID>::const_pointer data() const noexcept {
        return m_denseToEntity.data();
    }

    iterator begin() noexcept override {
        const auto pos = m_denseToEntity.size();
        return iterator{m_denseToEntity,pos};
    }

    iterator end() noexcept override {
        return iterator{m_denseToEntity,{}};
    }

    const_iterator begin() const noexcept override {
        const auto pos = m_denseToEntity.size();
        return const_iterator{m_denseToEntity, pos };
    }

    const_iterator end() const noexcept override {
        return const_iterator{m_denseToEntity, std::ptrdiff_t{} };
    }

    reverse_iterator rbegin() noexcept override {return std::make_reverse_iterator(end());}
    reverse_iterator rend() noexcept override {return std::make_reverse_iterator(begin());}

    const_reverse_iterator crbegin() const noexcept override {return std::make_reverse_iterator(end());
    }
    const_reverse_iterator crend() const noexcept override{
        return std::make_reverse_iterator(begin());
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
inline const T& SparseSetImpl<T, false>::GetRef(EntityID entity) const
{
    size_t index = GetDenseIndex(entity);
    if (index == NULL_INDEX)
        ASSERT(false,"GetRef called on invalid entity with " << EntityInfo(entity));

    return m_dense[index];
}

template<typename T>
inline T& SparseSetImpl<T, false>::GetRef(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    if (index == NULL_INDEX)
        ASSERT(false, "GetRef called on invalid entity with " << EntityInfo(entity));

    return m_dense[index];
}

template<typename T>
inline size_t SparseSetImpl<T, false>::Index(const EntityID entity) const
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
inline size_t SparseSetImpl<T, false>::Size() const noexcept
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
inline std::vector<EntityID>& SparseSetImpl<T, false>::GetEntityList() noexcept
{
    return m_denseToEntity;
}

template<typename T>
inline std::vector<T>& SparseSetImpl<T, false>::GetValues() noexcept
{
    return m_dense;
}

template<typename T>
inline bool SparseSetImpl<T, false>::ContainsEntity(const EntityID entity) const noexcept
{
    size_t index = GetDenseIndex(entity);

    return (index != NULL_INDEX && m_denseToEntity[index] == entity);
}

template<typename T>
inline size_t SparseSetImpl<T, false>::GetDenseIndex(const EntityID entity) const
{
    size_t index = GetEntityIndex(entity);
    size_t page = index / SPARSE_MAX_SIZE;
    size_t sparseIndex = index % SPARSE_MAX_SIZE;

    if (page < m_sparsePages.size()) {
        const Sparse& sparse = m_sparsePages[page];
        return sparse[sparseIndex];
    }

    return NULL_INDEX;
}

template<typename T>
inline void SparseSetImpl<T, false>::SetSparseIndex(const EntityID entity,const size_t index)
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
    using type = T;

    template<typename... Args>
    T* Emplace(const EntityID entity, Args&&... args){

        (void)sizeof...(args);

        if (entity == INVALID_ENTITY) return nullptr;

        // スパース／デンスにエンティティだけ追加
        SetSparseIndex(entity, m_denseToEntity.size());
        m_denseToEntity.push_back(entity);

        return nullptr;
    }

    template<typename... Args>
    T* Update(const EntityID entity, Args&&... args) {
        return nullptr;
    };

    // エンティティに対応するコンポーネントを取得する
    T* Get(const EntityID entity){return nullptr;};

    // 参照を返す
    T& GetRef(const EntityID entity){
        ASSERT(false,"do not use Empty struct pool GetRef()!!");
        return T();
    };

    size_t Index(const EntityID entity)const override{
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
    size_t Size() const noexcept override{
        return m_denseToEntity.size();
    };

    EntityID GetEntity(const std::size_t position) const override{
        if (position < m_denseToEntity.size()) return m_denseToEntity[position];

        return INVALID_ENTITY;
    };

    //登録されているTオブジェ所持のEntityを配列で返す
    std::vector<EntityID>& GetEntityList() noexcept override{
        return m_denseToEntity;
    };

    //対象のEntityがTオブジェを所持しているか
    bool ContainsEntity(const EntityID entity) const noexcept override{
        size_t index = GetDenseIndex(entity);

        return (index != NULL_INDEX && m_denseToEntity[index] == entity);
    };

    //対象のEntityのdenseIndexを取得
    inline size_t GetDenseIndex(const EntityID entity) const{
        size_t index = GetEntityIndex(entity);
        size_t page = index / SPARSE_MAX_SIZE;
        size_t sparseIndex = index % SPARSE_MAX_SIZE;

        if (page < m_sparsePages.size()) {
            const Sparse& sparse = m_sparsePages[page];
            return sparse[sparseIndex];
        }

        return NULL_INDEX;
    };

    //Sparse配列に設定
    inline void SetSparseIndex(const EntityID entity,const size_t index){
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

    std::vector<EntityID>::const_pointer data() const noexcept {
        return m_denseToEntity.data();
    }

    iterator begin() noexcept override {
        const auto pos = m_denseToEntity.size();
        return iterator{ m_denseToEntity,pos };
    }

    iterator end() noexcept override {
        return iterator{ m_denseToEntity,{} };
    }

    const_iterator begin() const noexcept override {
        const auto pos = m_denseToEntity.size();
        return const_iterator{ m_denseToEntity, pos };
    }

    const_iterator end() const noexcept override {
        return const_iterator{ m_denseToEntity, std::ptrdiff_t{} };
    }

    reverse_iterator rbegin() noexcept override { return std::make_reverse_iterator(end()); }
    reverse_iterator rend() noexcept override { return std::make_reverse_iterator(begin()); }

    const_reverse_iterator crbegin() const noexcept override {
        return std::make_reverse_iterator(end());
    }
    const_reverse_iterator crend() const noexcept override {
        return std::make_reverse_iterator(begin());
    }

private:
    std::vector<Sparse> m_sparsePages; // 疎テーブル
    std::vector<EntityID>m_denseToEntity;
    std::atomic<uint32_t> n_reserved;
};



}//namespace ECS
