#pragma once
#include "SystemGroup.h"

namespace ECS::System {
    struct PreCleanup {};
    struct Cleanup {};
    struct PostCleanup {};

    class CleanUpGroup : public SystemGroup {
    public:
        void onCreate(ECS::World& world) override;
    };
}