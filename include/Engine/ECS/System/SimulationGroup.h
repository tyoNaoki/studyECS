#pragma once
#include "SystemGroup.h"


namespace ECS::System {
    struct PreUpdate {};
    struct FixedUpdate {};
    struct Update {};
    struct PostUpdate{};
    struct LateUpdate{};

    class SimulationGroup : public SystemGroup {
        size_t preUpdateIndex;
        size_t fixedStepSimulationIndex;
        size_t updateIndex;
        size_t postUpdateIndex;
        size_t lateSimulationIndex;

    public:
        void onCreate(ECS::World& world) override;

        void onUpdate(ECS::World& world) override;
    };
}