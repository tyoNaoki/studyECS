#pragma once
#include "SystemGroup.h"

namespace ECS::System {

struct Schedule : public SystemGroup {
    void onCreate(World&world) override;
};

}
