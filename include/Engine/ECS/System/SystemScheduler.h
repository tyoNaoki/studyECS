#pragma once
#include "SystemGroup.h"

namespace ECS::System {

struct SystemScheduler : public SystemGroup {
    void onCreate(World&world) override;
};

}
