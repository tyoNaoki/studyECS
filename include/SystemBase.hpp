#pragma once
#include <vector>
#include "Schedule.hpp"

namespace ECS::System{

    struct SystemBase {
        virtual ~SystemBase() = default;
        virtual bool shouldRun() const{return true;}
        virtual ECS::Schedule::SystemHandle update(ECS::Schedule::SystemHandle input) = 0;

        std::vector<ECS::Schedule::SystemHandle> before;
        std::vector<ECS::Schedule::SystemHandle> after;
    };

}
