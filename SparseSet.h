#pragma once

#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "HashFunctions.h"
#include "Debug.h"

namespace ECS{
constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

// ランタイムでのインターフェース
class ISparseSet {
public:
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

    using iterator = std::vector<EntityID>::iterator;
    using reverse_iterator = std::vector<EntityID>::reverse_iterator;
    using const_iterator = std::vector<EntityID>::const_iterator;
    using const_reverse_iterator = std::vector<EntityID>::const_reverse_iterator;

    virtual iterator begin() = 0;
    virtual const_iterator begin() const = 0;

    virtual iterator end() = 0;
    virtual const_iterator end() const = 0;

    virtual reverse_iterator rbegin() = 0;
    virtual const_reverse_iterator rbegin() const  = 0;

    virtual reverse_iterator rend() = 0;
    virtual const_reverse_iterator rend() const = 0;
};

template<typename T>
class SparseSet : public ISparseSet
{
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
    virtual ~SparseSet(){}

    using type = T;

    // オブジェクトをエンティティにセットする
    virtual T* Set(EntityID entity,T obj);

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
    std::vector<T> m_dense; // 密テーブル
    std::vector<EntityID>m_denseToEntity;
    std::atomic<uint32_t> n_reserved;
};

template<typename T>
inline T* SparseSet<T>::Set(EntityID entity, T obj)
{
    if(entity == INVALID_ENTITY) return nullptr;

    size_t index = GetDenseIndex(entity);

    if(index != NULL_INDEX)
    {
        m_dense[index] = obj;
        m_denseToEntity[index] = entity;
        return &m_dense[index];
    }

    SetSparseIndex(entity, m_dense.size());

    m_dense.push_back(obj);
    m_denseToEntity.push_back(entity);

    return &m_dense.back();
}

template<typename T>
inline T* SparseSet<T>::Get(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    return (index != NULL_INDEX)&&m_denseToEntity[index] == entity ? &m_dense[index] : nullptr;
}

template<typename T>
inline T& SparseSet<T>::GetRef(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    if (index == NULL_INDEX)
        ASSERT(false,"GetRef called on invalid entity with " << EntityInfo(entity));

    return m_dense[index];
}

template<typename T>
inline size_t SparseSet<T>::Index(EntityID entity)
{
    size_t index = GetDenseIndex(entity);
    if(index != NULL_INDEX && m_denseToEntity[index] == entity) return index;

    return NULL_INDEX;
}

template<typename T>
inline ecs_map::id_type SparseSet<T>::Hash() const
{
    return ecs_map::type_hash<T>();
}

template<typename T>
inline void SparseSet<T>::swap_elements(const EntityID lhs, const EntityID rhs)
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
inline void SparseSet<T>::Delete(EntityID entity)
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
inline void SparseSet<T>::Clear()
{
    m_sparsePages.clear();
    m_dense.clear();
    m_denseToEntity.clear();
}

template<typename T>
inline size_t SparseSet<T>::Size()
{
    return m_dense.size();
}

template<typename T>
inline EntityID SparseSet<T>::GetEntity(std::size_t position) const
{
    if(position < m_denseToEntity.size()) return m_denseToEntity[position];

    return INVALID_ENTITY;
}

template<typename T>
inline std::vector<EntityID> SparseSet<T>::GetEntityList()
{
    return m_denseToEntity;
}

template<typename T>
inline bool SparseSet<T>::ContainsEntity(EntityID entity)
{
    size_t index = GetDenseIndex(entity);

    return (index != NULL_INDEX && m_denseToEntity[index] == entity);
}

template<typename T>
inline size_t SparseSet<T>::GetDenseIndex(EntityID entity)
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
inline void SparseSet<T>::SetSparseIndex(EntityID entity, size_t index)
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
}//namespace ECS
