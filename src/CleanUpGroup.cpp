#include "CleanUpGroup.h"
#include "World.h"

void ECS::System::CleanUpGroup::onCreate(ECS::World& world)
{
    addSystem(world.createSystemGroup<SystemGroup, PreCleanup>());
    addSystem(world.createSystemGroup<SystemGroup, Cleanup>());
    addSystem(world.createSystemGroup<SystemGroup, PostCleanup>());
}
