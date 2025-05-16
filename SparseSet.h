#pragma once

#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "Debug.h"

constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

template <class... Types>
struct type_list {
    using type_tuple = std::tuple<Types...>;

    template <size_t Index>
    using get = std::tuple_element_t<Index, type_tuple>;

    static constexpr size_t size = sizeof...(Types);
};

template<typename... Type>
struct exclude_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit exclude_t() = default;
};

template<>
struct exclude_t<> final : type_list<> {
    explicit exclude_t() = default;
};

/**
 * @brief Variable template for exclusion lists.
 * @tparam Type List of types.
 */
template<typename... Type>
constexpr exclude_t<Type...> exclude{};

/**
 * @brief Alias for lists of observed elements.
 * @tparam Type List of types.
 */
template<typename... Type>
struct get_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit get_t() = default;
};

/**
 * @brief Variable template for lists of observed elements.
 * @tparam Type List of types.
 */
template<typename... Type>
static const get_t<Type...> get{};  // `inline constexpr` → `static const`

/**
 * @brief Alias for lists of owned elements.
 * @tparam Type List of types.
 */
template<typename... Type>
struct owned_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit owned_t() = default;
};

/**
 * @brief Variable template for lists of owned elements.
 * @tparam Type List of types.
 */
template<typename... Type>
static const owned_t<Type...> owned{};  // `inline constexpr` → `static const`

// Base class allows runtime polymorphism
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
    T* Set(EntityID entity,T obj);

    T* Get(EntityID entity);

    T& GetRef(EntityID entity);

    void Delete(EntityID entity) override;

    void Clear() override;

    size_t Size() override;

    std::vector<EntityID> GetEntityList() override;

    bool ContainsEntity(EntityID entity) override;

    inline size_t GetDenseIndex(EntityID entity);

    inline void SetSparseIndex(EntityID entity,size_t index);

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
