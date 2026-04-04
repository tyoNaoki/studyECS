#pragma once
#include "taskPtr.hpp"

namespace ECS::System{

    struct SystemBase {
        virtual ~SystemBase() = default;
        virtual ECS::JobSystem::JobHandle Update(ECS::JobSystem::JobHandle input) = 0;

        std::vector<std::type_index> before;
        std::vector<std::type_index> after;
    };

}
