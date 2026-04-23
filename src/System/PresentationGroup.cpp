#include "Engine\ECS\System\PresentationGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::PresentationGroup::onCreate(ECS::World& world)
{
    addSystem(world.registerSystemGroup<SystemGroup, PreRender>());
    addSystem(world.registerSystemGroup<SystemGroup, Render>());
    addSystem(world.registerSystemGroup<SystemGroup, PostRender>());
}
