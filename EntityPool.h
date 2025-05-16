#pragma once
#undef max

#include <atomic>
#include "Entity.h"
#include <vector>
#include <array>
#include <limits>

struct Entry {
    size_t denseIndex;
    EntityVersion version;
    size_t next_free;  // `SIZE_MAX` �Ȃ�󂫂Ȃ�

    bool Empty(){return denseIndex == std::numeric_limits<size_t>::max();}
};

class EntityPool {
public:
    EntityID alloc(std::string name = "");

    bool dealloc(EntityID& entity);

    bool contains(EntityID entity);

    size_t denseUseSize();

    std::string GetName(EntityID entity);

private:
    std::vector<Entry> m_sparse; // �a�e�[�u��
    std::vector<std::pair<std::string,EntityID>> m_dense; // ���e�[�u��
    size_t first_free = std::numeric_limits<size_t>::max(); // �󂫃X���b�g (`SIZE_MAX` �Ȃ疞�t)
    size_t n_free = 0;
    std::atomic<uint32_t> n_reserved;
};



