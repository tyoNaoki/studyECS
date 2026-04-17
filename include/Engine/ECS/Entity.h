#pragma once

#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <string>

namespace ECS::Entity{

using EntityIndex = uint32_t;
using EntityVersion = uint32_t;
using EntityType = uint32_t;
//using EntityID = uint64_t;

struct EntityID {
    EntityIndex index;
    EntityVersion version;

    constexpr bool operator==(const EntityID& rhs) const {
        return this->index == rhs.index &&
            this->version == rhs.version;
    }

    explicit operator uint64_t() const = delete;
};

inline EntityID CreateEntityId(EntityIndex index, EntityVersion version) {
    return EntityID{index,version};
}

inline EntityIndex GetEntityIndex(EntityID id) {
    return id.index;//上位32ビットを取得
}

//inline EntityID CreateEntityId(EntityIndex index, EntityVersion version) {
//    return ((EntityID)index << 32) | ((EntityID)version);
//}
//
//inline EntityIndex GetEntityIndex(EntityID id) {
//    return id >> 32;//上位32ビットを取得
//}

//inline EntityVersion GetEntityVersion(EntityID id) {
//    return static_cast<EntityVersion>(id & 0xFFFFFFFF); // 下位32ビットを取得
//}

inline std::string EntityInfo(EntityID id) {
    return "[ENTITYID : '" + std::to_string(GetEntityIndex(id)) + "]";
}

inline EntityVersion GetEntityVersion(EntityID id) {
    return id.version; // 下位32ビットを取得
}

inline bool IsEntityValid(EntityID id) {
    return GetEntityIndex(id) != 0xFFFFFFFF; // 最大値と比較する
}

#define INVALID_ENTITY CreateEntityId(0xFFFFFFFF, 0)

}

#endif // ENTITY_H