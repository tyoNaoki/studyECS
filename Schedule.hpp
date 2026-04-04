#pragma once
#include "SystemGroup.hpp"

namespace ECS::Schedule {

struct Schedule {
    ECS::System::SystemGroup initialization;
    ECS::System::SystemGroup simulation;
    ECS::System::SystemGroup presentation;

    ECS::JobSystem::JobHandle Update();
};

}
