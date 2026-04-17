#pragma once
#undef max

#include "Entity.h"
#include <vector>
#include <array>
#include <limits>
#include "EntityEventMixin.hpp"

namespace ECS{

struct EntityIterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = Entity::EntityID;
    using reference = Entity::EntityID&;
    using pointer = Entity::EntityID*;

    using DenseIterator = std::vector<std::pair<std::string, Entity::EntityID>>::iterator;

    explicit EntityIterator(DenseIterator it) : m_it(it) {}

    Entity::EntityID operator*() const { return m_it->second; }

    EntityIterator& operator++() { ++m_it; return *this; }
    EntityIterator  operator++(int) { auto tmp = *this; ++m_it; return tmp; }

    bool operator==(const EntityIterator& other) const { return m_it == other.m_it; }
    bool operator!=(const EntityIterator& other) const { return !(*this == other); }

private:
    DenseIterator m_it;
};

struct Entry {
    size_t denseIndex;
    Entity::EntityVersion version;
    size_t next_free;  // `SIZE_MAX` なら空きなし

    bool Empty(){return denseIndex == std::numeric_limits<size_t>::max();}
};

class EntityPool 
    : public EntityEventMixin<EntityPool>
{
    using Pack = std::pair<std::string, Entity::EntityID>;
    using SparseContainer = std::vector<Entry>;
    using DenseContainer = std::vector<Pack>;

public:
    Entity::EntityID alloc(std::string name = "");

    bool dealloc(Entity::EntityID& entity);

    bool contains(const Entity::EntityID& entity);

    size_t denseUseSize();

    std::string GetName(Entity::EntityID entity);

    EntityIterator begin() { return EntityIterator(m_dense.begin()); }
    EntityIterator end() { return EntityIterator(m_dense.end()); }

private:
    SparseContainer m_sparse; // 疎テーブル
    DenseContainer m_dense; // 密テーブル
    size_t first_free = std::numeric_limits<size_t>::max();// 空きスロット (`SIZE_MAX` なら満杯)
    size_t n_free = 0;
};

}//namespace ECS



