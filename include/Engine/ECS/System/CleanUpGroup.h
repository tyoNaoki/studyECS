#pragma once
#include "SystemGroup.h"

namespace ECS::System {
    struct PreCleanup {};
    struct Cleanup {};
    struct PostCleanup {};

    struct CleanUpJobManagerSystem : public SystemBase {
        void onUpdate(ECS::World&world) override;
    };

    class CleanUpGroup : public SystemGroup {
    public:
        void onCreate(ECS::World& world) override;
    };
}