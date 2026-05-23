#include "Engine\ECS\System\SimulationGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::SimulationGroup::onCreate(ECS::World& world)
{
    auto handle = world.registerSystemGroup<SystemGroup, PreUpdate>();
    preUpdateIndex = world.getSystemEntry(handle).index;

    handle = world.registerSystemGroup<SystemGroup, FixedUpdate>();
    fixedStepSimulationIndex = world.getSystemEntry(handle).index;

    handle = world.registerSystemGroup<SystemGroup, Update>();
    updateIndex = world.getSystemEntry(handle).index;

    handle = world.registerSystemGroup<SystemGroup, PostUpdate>();
    postUpdateIndex = world.getSystemEntry(handle).index;

    handle = world.registerSystemGroup<SystemGroup, LateUpdate>();
    lateSimulationIndex = world.getSystemEntry(handle).index;
}

void ECS::System::SimulationGroup::onUpdate(ECS::World& world)
{
    if (dirty) sort(world);

    world.getSystemGroup(preUpdateIndex)->onUpdate(world);

    auto& time = world.getTime();
    time.accumulator += time.deltaTime;
    while (time.accumulator >= time.fixedDeltaTime)
    {
        world.getSystemGroup(fixedStepSimulationIndex)->onUpdate(world);
        time.accumulator -= time.fixedDeltaTime;
    }

    world.getSystemGroup(updateIndex)->onUpdate(world);
    world.getSystemGroup(postUpdateIndex)->onUpdate(world);
    world.getSystemGroup(lateSimulationIndex)->onUpdate(world);

    if(sorted.size()==0) return;

    //ソート済みのものを実行
    for (int i = 0; i < sorted.size(); i++) {
        auto& entry = world.getSystemEntry(sorted[i]);
        if (entry.isGroup) {
            world.getSystemGroup(entry.index)->onUpdate(world);
        }
        else {
            world.getSystem(entry.index)->onUpdate(world);
        }
    }
}
