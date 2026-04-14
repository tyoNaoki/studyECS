#include "Engine\ECS\System\PresentationGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::PresentationGroup::onCreate(ECS::World& world)
{
    addSystem(world.createSystemGroup<SystemGroup, PreRender>());
    addSystem(world.createSystemGroup<SystemGroup, Render>());
    addSystem(world.createSystemGroup<SystemGroup, PostRender>());
}
