#pragma once
#include "SystemGroup.h"

namespace ECS::System {
    class PreInitializationGroup : public SystemGroup {

    public:
        PreInitializationGroup(ECS::World& world) {
        }
    };
}
