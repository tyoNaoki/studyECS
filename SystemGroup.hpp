#pragma once
#include "SystemBase.hpp"

namespace ECS::System{

    struct SystemGroup : SystemBase {
        std::vector<SystemBase*> systems;
        std::vector<SystemBase*> sorted;
        bool dirty = true;

        void Add(SystemBase* s){};
        void Sort(){};
        ECS::JobSystem::JobHandle Update(ECS::JobSystem::JobHandle input) override{};
    };

}