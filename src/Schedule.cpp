#include "Schedule.h"
#include "InitializationGroup.h"
#include "SimulationGroup.h"
#include "PresentationGroup.h"
#include "CleanUpGroup.h"
#include "World.h"

void ECS::System::Schedule::onCreate(World& world)
{
    auto initialization = world.createSystemGroup<System::InitializationGroup, System::InitializationGroup>();
    auto simulation = world.createSystemGroup<System::SimulationGroup, System::SimulationGroup>();
    auto presentation = world.createSystemGroup<System::PresentationGroup, System::PresentationGroup>();
    auto cleanup = world.createSystemGroup<System::CleanUpGroup, System::CleanUpGroup>();
    addSystem(initialization);
    addSystem(simulation);
    addSystem(presentation);
    addSystem(cleanup);

    world.getSystem(initialization).groupClass->onCreate(world);
    world.getSystem(simulation).groupClass->onCreate(world);
    world.getSystem(presentation).groupClass->onCreate(world);
    world.getSystem(cleanup).groupClass->onCreate(world);

    world.addBefore(initialization, simulation);
    world.addBefore(simulation, presentation);
    world.addBefore(presentation, cleanup);
}
