#include "CleanUpGroup.h"
#include "World.h"
#include "JobManager.h"

void ECS::System::CleanUpGroup::onCreate(ECS::World& world)
{
    addSystem(world.createSystemGroup<SystemGroup, PreCleanup>());
    auto cleanUpID = world.createSystemGroup<SystemGroup, Cleanup>();
    addSystem(cleanUpID);
    addSystem(world.createSystemGroup<SystemGroup, PostCleanup>());
    
    
    world.addSystem<Cleanup>([](World& w) {
        JobSystem::JobManager::Instance().cleanUpMainThreadOnLastFrame();
        });
}
