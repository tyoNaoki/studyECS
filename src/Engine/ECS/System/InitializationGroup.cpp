#include "Engine\ECS\System\InitializationGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::InitializationGroup::onCreate(ECS::World& world)
{
    addSystem(world.registerSystemGroup<SystemGroup, PreInitialization>());
    addSystem(world.registerSystemGroup<SystemGroup, Initialization>());
    addSystem(world.registerSystemGroup<SystemGroup, PostInitialization>());
}
