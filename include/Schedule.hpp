#pragma once
#include "SystemGroup.hpp"

namespace ECS::Schedule {

    enum class SystemGroupTag {
        initialization,
        simulation,
        presentation
    };

struct Schedule {
    Schedule() = default;

    ECS::System::SystemGroup initialization;
    ECS::System::SystemGroup simulation;
    ECS::System::SystemGroup presentation;

    void addSystem(SystemGroupTag tag, ECS::System::SystemBase* system){
        switch (tag)
        {
        case ECS::Schedule::SystemGroupTag::initialization:
            initialization.add(system);
            break;
        case ECS::Schedule::SystemGroupTag::simulation:
            simulation.add(system);
            break;
        case ECS::Schedule::SystemGroupTag::presentation:
            presentation.add(system);
            break;
        default:
            break;
        }
    }

    void sort(){
        initialization.sort();
        simulation.sort();
        presentation.sort();
    }

    void update(){
        initialization.update();
        simulation.update();
        presentation.update();
    };
};

}
