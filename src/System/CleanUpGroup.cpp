#include "Engine\ECS\System\CleanUpGroup.h"
#include "Engine\ECS\World.h"
#include "Engine\Core\JobManager.h"

void ECS::System::CleanUpGroup::onCreate(ECS::World& world)
{
    addSystem(world.registerSystemGroup<SystemGroup, PreCleanup>());
    addSystem(world.registerSystemGroup<SystemGroup, Cleanup>());
    addSystem(world.registerSystemGroup<SystemGroup, PostCleanup>());
    
    world.registerSystem<CleanUpJobManagerSystem,Cleanup>();
}

void ECS::System::CleanUpJobManagerSystem::onUpdate(ECS::World& world)
{
    JobSystem::JobManager::Instance().cleanUpMainThreadOnLastFrame();
}
