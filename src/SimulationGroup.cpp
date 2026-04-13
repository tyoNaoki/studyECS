#include "SimulationGroup.h"
#include "World.h"

void ECS::System::SimulationGroup::onCreate(ECS::World& world)
{
    auto tempSystemID = world.createSystemGroup<SystemGroup, PreUpdate>();
    preUpdateID = world.getSystem(tempSystemID).groupID;

    tempSystemID = world.createSystemGroup<SystemGroup, FixedUpdate>();
    fixedStepSimulationID = world.getSystem(tempSystemID).groupID;

    tempSystemID = world.createSystemGroup<SystemGroup, Update>();
    updateID = world.getSystem(tempSystemID).groupID;

    tempSystemID = world.createSystemGroup<SystemGroup, PostUpdate>();
    postUpdateID = world.getSystem(tempSystemID).groupID;

    tempSystemID = world.createSystemGroup<SystemGroup, LateUpdate>();
    lateSimulationID = world.getSystem(tempSystemID).groupID;
}

void ECS::System::SimulationGroup::onUpdate(ECS::World& world)
{
    if (dirty) sort(world);

    world.getSystemGroup(preUpdateID)->onUpdate(world);

    auto& time = world.getTime();
    time.accumulator += time.deltaTime;
    while (time.accumulator >= time.fixedDeltaTime)
    {
        world.getSystemGroup(fixedStepSimulationID)->onUpdate(world);
        time.accumulator -= time.fixedDeltaTime;
    }

    world.getSystemGroup(updateID)->onUpdate(world);
    world.getSystemGroup(postUpdateID)->onUpdate(world);
    world.getSystemGroup(lateSimulationID)->onUpdate(world);

    if(sorted.size()==0) return;

    //ソート済みのものを実行
    for (int i = 0; i < sorted.size(); i++) {
        auto& entry = world.getSystem(sorted[i]);
        if (entry.fn) {
            entry.fn(world);
        }
        else {
            world.getSystemGroup(entry.groupID)->onUpdate(world);
        }
    }
}
