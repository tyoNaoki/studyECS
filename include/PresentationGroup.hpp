#pragma once
#include "SystemGroup.h"

namespace ECS::System {
    class PresentationGroup : public SystemGroup {
        void onUpdate(ECS::World& world) override {
            if (dirty) sort(world);

            for (int i = 0; i < sorted.size(); i++) {
                world.getSystem(sorted[i]).fn(world);
            }
        };
    };
}
