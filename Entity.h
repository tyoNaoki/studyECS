#pragma once

#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <string>

typedef uint32_t EntityIndex;
typedef uint32_t EntityVersion;
typedef  uint64_t EntityID;

inline EntityID CreateEntityId(EntityIndex index, EntityVersion version) {
    return ((EntityID)index << 32) | ((EntityID)version);
}

inline EntityIndex GetEntityIndex(EntityID id) {
    return id >> 32;
}

inline std::string EntityInfo(EntityID id) {
    return "[ENTITYID : '" + std::to_string(GetEntityIndex(id)) + "]";
}

inline EntityVersion GetEntityVersion(EntityID id) {
    return static_cast<EntityVersion>(id & 0xFFFFFFFF); // 下位32ビットを取得
}

inline bool IsEntityValid(EntityID id) {
    return GetEntityIndex(id) != 0xFFFFFFFF; // 最大値と比較する
}

#define INVALID_ENTITY CreateEntityId(0xFFFFFFFF, 0)

#endif // ENTITY_H