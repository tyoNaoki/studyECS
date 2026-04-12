#pragma once
#include "SystemGroup.h"


namespace ECS::System {
    struct PreUpdate {};
    struct FixedUpdate {};
    struct Update {};
    struct PostUpdate{};
    struct LateUpdate{};

    class SimulationGroup : public SystemGroup {
        ECS::System::SystemID preUpdateID;
        ECS::System::SystemID fixedStepSimulationID;
        ECS::System::SystemID updateID;
        ECS::System::SystemID postUpdateID;
        ECS::System::SystemID lateSimulationID;

    public:
        void onCreate(ECS::World& world) override;

        void onUpdate(ECS::World& world) override;
    };
}