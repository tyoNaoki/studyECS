#include "Engine\ECS\System\SystemScheduler.h"
#include "Engine\ECS\System\InitializationGroup.h"
#include "Engine\ECS\System\SimulationGroup.h"
#include "Engine\ECS\System\PresentationGroup.h"
#include "Engine\ECS\System\CleanUpGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::SystemScheduler::onCreate(World& world)
{
    auto initialization = world.registerSystemGroup<InitializationGroup, InitializationGroup>();
    auto simulation = world.registerSystemGroup<SimulationGroup, SimulationGroup>();
    auto presentation = world.registerSystemGroup<PresentationGroup, PresentationGroup>();
    auto cleanup = world.registerSystemGroup<CleanUpGroup, CleanUpGroup>();
    addSystem(initialization);
    addSystem(simulation);
    addSystem(presentation);
    addSystem(cleanup);

    world.addBefore(initialization, simulation);
    world.addBefore(simulation, presentation);
    world.addBefore(presentation, cleanup);
}
