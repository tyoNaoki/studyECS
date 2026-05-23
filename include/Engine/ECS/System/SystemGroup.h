#pragma once
#include <queue>
#include <algorithm>
#include "SystemBase.hpp"

namespace ECS{
    class World;

namespace System{

    class SystemGroup {
    public:
        virtual ~SystemGroup() = default;

        void addSystem(SystemHandle s){
            systems.push_back(s);
            dirty = true;
        };

        virtual void onCreate(World& world){}

        virtual void onUpdate(World& world);

        void sort(World& world) {

            topologicalSort(world, *this);
            dirty = false;
        };

    protected:
        
        bool dirty = false;
        std::vector<ECS::System::SystemHandle> sorted;
    private:
        void topologicalSort(ECS::World& world, SystemGroup& group);

    private:
        std::vector<ECS::System::SystemHandle> systems;
    };

}
}