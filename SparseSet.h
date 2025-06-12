#pragma once

#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "Debug.h"

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
};

template<typename T>
class SparseSet : public ISparseSet
{
private:
    static constexpr size_t SPARSE_MAX_SIZE = 2048;

    using Sparse = std::array<size_t,SPARSE_MAX_SIZE>;

public:
    virtual ~SparseSet(){}

    using type = T;

    // オブジェクトをエンティティにセットする
    T* Set(EntityID entity,T obj);

    // エンティティに対応するコンポーネントを取得する
    T* Get(EntityID entity);

    // 参照を返す
    T& GetRef(EntityID entity);

    // 指定エンティティのコンポーネントを削除する
    void Delete(EntityID entity) override;

    //内部のTオブジェをすべて削除
    void Clear() override;

    //現在のdense配列の大きさ取得
    size_t Size() override;

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
