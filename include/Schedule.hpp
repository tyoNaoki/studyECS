#pragma once
#include "InitializationGroup.h"
#include "SimulationGroup.hpp"
#include "PresentationGroup.hpp"
#include "CleanUpGroup.hpp"

namespace ECS::Schedule {

    enum class SystemGroupTag {
        initialization,
        simulation,
        presentation,
        cleanup
    };

struct Schedule {
    Schedule(ECS::World&w) : world(w){
        initialization = world.createGroup<System::InitializationGroup>();
        simulation = world.createGroup<System::SimulationGroup>();
        presentation = world.createGroup<System::PresentationGroup>();
        cleanup = world.createGroup<System::CleanUpGroup>();
    }

    ECS::System::SystemID initialization;
    ECS::System::SystemID simulation;
    ECS::System::SystemID presentation;
    ECS::System::SystemID cleanup;

    ECS::World& world;

    void sort(ECS::World& world){
        world.getSystem(initialization).groupClass->sort(world);
        world.getSystem(simulation).groupClass->sort(world);
        world.getSystem(presentation).groupClass->sort(world);
        world.getSystem(cleanup).groupClass->sort(world);
    }

    void update(ECS::World& world){
        world.getSystem(initialization).groupClass->onUpdate(world);
        world.getSystem(simulation).groupClass->onUpdate(world);
        world.getSystem(presentation).groupClass->onUpdate(world);
        world.getSystem(cleanup).groupClass->onUpdate(world);
    };

};

}
