#pragma once
#include "SystemBase.hpp"

namespace ECS::System{

    struct SystemGroup : public SystemBase {
        std::vector<SystemBase*> systems;
        std::vector<SystemBase*> sorted;
        bool dirty = true;

        void add(SystemBase* s){
            systems.push_back(s);
        };
        void sort(){
        };

        ECS::Schedule::SystemHandle update(ECS::Schedule::SystemHandle input) override{
            for(int i=0;i<sorted.size();i++){
                input = sorted[i]->update(input);
            }
            return input;
        };
    };

}