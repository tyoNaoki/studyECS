#pragma once
#include <queue>
#include <algorithm>
#include "SystemBase.hpp"

namespace ECS{
    class World;

namespace System{

    class SystemGroup {
        std::vector<ECS::System::SystemID> systems;

    public:
        virtual ~SystemGroup() = default;

        void addSystem(SystemID s){
            systems.push_back(s);
            dirty = true;
        };

        virtual void onCreate(ECS::World& world){}

        virtual void onUpdate(ECS::World& world);

        void sort(ECS::World& world) {

            topologicalSort(world, *this);
            dirty = false;
        };

    protected:
        
        bool dirty = true;
        std::vector<ECS::System::SystemID> sorted;
    private:
        void topologicalSort(ECS::World& world, SystemGroup& group);
    };

}
}