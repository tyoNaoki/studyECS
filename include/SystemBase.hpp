#pragma once
#include <vector>
#include <memory>

namespace ECS{
    class World;
    
    using SystemFn = void(*)(World&);

namespace System{

    class SystemGroup;
    using SystemID = size_t;
    
    struct SystemEntry {
        SystemFn fn;
        std::unique_ptr<SystemGroup>groupClass;
        std::vector<SystemID> before;
        std::vector<SystemID> after;
        SystemID id;
    };
}
}
