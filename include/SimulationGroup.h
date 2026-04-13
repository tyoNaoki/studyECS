#pragma once
#include "SystemGroup.h"


namespace ECS::System {
    struct PreUpdate {};
    struct FixedUpdate {};
    struct Update {};
    struct PostUpdate{};
    struct LateUpdate{};

    class SimulationGroup : public SystemGroup {
        ECS::System::GroupID preUpdateID;
        ECS::System::GroupID fixedStepSimulationID;
        ECS::System::GroupID updateID;
        ECS::System::GroupID postUpdateID;
        ECS::System::GroupID lateSimulationID;

    public:
        void onCreate(ECS::World& world) override;

        void onUpdate(ECS::World& world) override;
    };
}