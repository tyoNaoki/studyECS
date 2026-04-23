#pragma once
#include <vector>
#include <memory>

namespace ECS{
    class World;

namespace System{

    class SystemGroup;

    struct SystemHandle
    {
        size_t ID;
    };

    class SystemBase {
    public:
        SystemBase() = default;
        virtual ~SystemBase() = default;

        //コピー禁止
        SystemBase(const SystemBase&) = delete;
        SystemBase& operator=(const SystemBase&) = delete;

        SystemBase(SystemBase&&) = delete;
        SystemBase& operator=(SystemBase&&) = delete;

        virtual void onCreate(ECS::World& world) {}
        virtual void onUpdate(ECS::World& world) = 0;

        //有効/無効化
        bool enabled = true;
    };

    
    struct SystemEntry {
        std::vector<SystemHandle> before;
        std::vector<SystemHandle> after;
        size_t index;
        bool isGroup = false;
    };
}
}
