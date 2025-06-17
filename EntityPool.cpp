#include "EntityPool.h"
#include <stdexcept>
#include "Debug.h"

EntityID EntityPool:: alloc(std::string name)
{
    if (first_free != std::numeric_limits<size_t>::max() && n_free > 0) { // 空きスロットあり
        auto free_index = first_free;
        Entry& entry = m_sparse[free_index];

        if(!entry.Empty())
        {
            ASSERT(false,"free slot bug!!");
        }

        EntityID entity = CreateEntityId((EntityIndex)free_index, entry.version++);

        first_free = entry.next_free; // 次の空きスロット
        n_free--;

        if(n_free < 0)
        {
            ASSERT(false, "n_free < 0!!");
            n_free = 0;
        }

        auto denceIndex = denseUseSize()-1;
        m_dense[denceIndex] = {name,entity};
        m_sparse[free_index] = {denceIndex,entry.version,std::numeric_limits<size_t>::max() };
        return entity;
    }

    auto index = m_dense.size();

    auto entity = CreateEntityId((EntityIndex)index, 0);
    m_dense.push_back({name,entity});
    m_sparse.push_back({index,0,std::numeric_limits<size_t>::max() });

    return entity;
}

bool EntityPool::dealloc(EntityID& entity)
{
    auto entityIndex = (size_t)GetEntityIndex(entity);
    if(entityIndex > m_sparse.size()-1||!IsEntityValid(entity)) return false;

    if(m_sparse[entityIndex].Empty())return false;

    auto denseIndex = m_sparse[entityIndex].denseIndex;

    auto version = GetEntityVersion(entity);
    if (!IsEntityValid(m_dense[denseIndex].second)||GetEntityVersion(m_dense[denseIndex].second) != version) return false;

    //sparseの該当要素を使用不可に設定
    m_sparse[entityIndex] = { std::numeric_limits<size_t>::max(),version,first_free};

    //要素数が一つだけの場合
    if (denseUseSize() == 1) {
        m_dense[denseIndex].second = INVALID_ENTITY;
    }
    else {
        size_t lastIndex = denseUseSize() - 1;
        auto lastEntity = m_dense[lastIndex];
        
        // 削除対象をdenseの最後の要素で上書き
        m_dense[denseIndex] = {lastEntity.first,lastEntity.second};
        m_dense.pop_back();

        m_sparse[(size_t)GetEntityIndex(lastEntity.second)] = { denseIndex, GetEntityVersion(lastEntity.second), std::numeric_limits<size_t>::max() };
    }

    first_free = entityIndex;
    n_free ++;

    entity = INVALID_ENTITY;

    return true;
}

bool EntityPool::contains(EntityID entity)
{
    auto index = (size_t)GetEntityIndex(entity);

    if(index >= m_sparse.size()||m_sparse[index].Empty()||m_sparse[index].denseIndex > denseUseSize()-1) return false;

    return m_dense[m_sparse[index].denseIndex].second == entity;
}

size_t EntityPool::denseUseSize()
{
    return m_dense.size()-n_free;
}

std::string EntityPool::GetName(EntityID entity)
{
    auto index = (size_t)GetEntityIndex(entity);

    if(index >= m_sparse.size()) return "NULL";

    return m_dense[m_sparse[index].denseIndex].first;
}
