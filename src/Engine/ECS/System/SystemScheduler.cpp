#include "Engine\ECS\System\SystemScheduler.h"
#include "Engine\ECS\System\InitializationGroup.h"
#include "Engine\ECS\System\SimulationGroup.h"
#include "Engine\ECS\System\PresentationGroup.h"
#include "Engine\ECS\System\CleanUpGroup.h"
#include "Engine\ECS\System\SystemGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::SystemScheduler::initialize(World& world)
{
    auto initializationHandle = world.registerSystemGroup<InitializationGroup, InitializationGroup>();
    auto simulationHandle = world.registerSystemGroup<SimulationGroup, SimulationGroup>();
    auto presentationHandle = world.registerSystemGroup<PresentationGroup, PresentationGroup>();
    auto cleanupHandle = world.registerSystemGroup<CleanUpGroup, CleanUpGroup>();
    initializationIndex = world.getSystemEntry(initializationHandle).index;
    simulationIndex = world.getSystemEntry(simulationHandle).index;
    presentationIndex = world.getSystemEntry(presentationHandle).index;
    cleanupIndex = world.getSystemEntry(cleanupHandle).index;
}

void ECS::System::SystemScheduler::onUpdate(World& world)
{
    world.getSystemGroup(initializationIndex)->onUpdate(world);
    world.getSystemGroup(simulationIndex)->onUpdate(world);
}

void ECS::System::SystemScheduler::onRender(ECS::World& world)
{
    //描画系のシステムを呼び出す
    world.getSystemGroup(presentationIndex)->onUpdate(world);
}

void ECS::System::SystemScheduler::onCleanup(ECS::World& world)
{
    //クリーンアップ系のシステムを呼び出す
    world.getSystemGroup(cleanupIndex)->onUpdate(world);
}
