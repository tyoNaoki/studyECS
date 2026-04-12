#include "SimulationGroup.h"
#include "World.h"

void ECS::System::SimulationGroup::onCreate(ECS::World& world)
{
    preUpdateID = world.createSystemGroup<SystemGroup, PreUpdate>();
    addSystem(preUpdateID);

    fixedStepSimulationID = world.createSystemGroup<SystemGroup, FixedUpdate>();
    addSystem(fixedStepSimulationID);

    updateID = world.createSystemGroup<SystemGroup, Update>();
    addSystem(updateID);

    postUpdateID = world.createSystemGroup<SystemGroup, PostUpdate>();
    addSystem(postUpdateID);

    lateSimulationID = world.createSystemGroup<SystemGroup, LateUpdate>();
    addSystem(lateSimulationID);
}

void ECS::System::SimulationGroup::onUpdate(ECS::World& world)
{
    if (dirty) sort(world);

    world.getSystem(preUpdateID).groupClass->onUpdate(world);

    {

    }
    //専用固有ロジック
    //fixedUpdateだけ固有フレームで回すようにする。
}
