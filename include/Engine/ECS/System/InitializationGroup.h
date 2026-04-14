#pragma once
#include "SystemGroup.h"

namespace ECS::System{

    struct PreInitialization {};
    struct Initialization {};
    struct PostInitialization {};

class InitializationGroup : public SystemGroup {
public:

    void onCreate(ECS::World&world) override;
};

}