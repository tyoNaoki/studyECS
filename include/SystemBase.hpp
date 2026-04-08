#pragma once
#include <vector>

namespace ECS::System{

    using SystemID = size_t;
    class World;

    struct SystemBase {
        virtual ~SystemBase() = default;
        virtual bool shouldRun() const{return true;}
        virtual void update() = 0;

        std::vector<SystemBase*> before;
        std::vector<SystemBase*> after;
        SystemID systemId;
        World& world;

    protected:
        SystemBase(World& w) : world(w) {}
        
    };

}
