#include "Schedule.h"
#include "InitializationGroup.h"
#include "SimulationGroup.h"
#include "PresentationGroup.h"
#include "CleanUpGroup.h"
#include "World.h"

void ECS::System::Schedule::onCreate(World& world)
{
    auto initialization = world.createSystemGroup<InitializationGroup, InitializationGroup>();
    auto simulation = world.createSystemGroup<SimulationGroup, SimulationGroup>();
    auto presentation = world.createSystemGroup<PresentationGroup, PresentationGroup>();
    auto cleanup = world.createSystemGroup<CleanUpGroup, CleanUpGroup>();
    addSystem(initialization);
    addSystem(simulation);
    addSystem(presentation);
    addSystem(cleanup);

    world.getSystemGroup(world.getSystem(initialization).groupID)->onCreate(world);
    world.getSystemGroup(world.getSystem(simulation).groupID)->onCreate(world);
    world.getSystemGroup(world.getSystem(presentation).groupID)->onCreate(world);
    world.getSystemGroup(world.getSystem(cleanup).groupID)->onCreate(world);

    world.addBefore(initialization, simulation);
    world.addBefore(simulation, presentation);
    world.addBefore(presentation, cleanup);


}
