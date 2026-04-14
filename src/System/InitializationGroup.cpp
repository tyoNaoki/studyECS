#include "Engine\ECS\System\InitializationGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::InitializationGroup::onCreate(ECS::World& world)
{
    addSystem(world.createSystemGroup<SystemGroup, PreInitialization>());
    addSystem(world.createSystemGroup<SystemGroup, Initialization>());
    addSystem(world.createSystemGroup<SystemGroup, PostInitialization>());
}
