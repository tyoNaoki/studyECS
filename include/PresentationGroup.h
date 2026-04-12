#pragma once
#include "SystemGroup.h"

namespace ECS::System {
    struct PreRender {};
    struct Render {};
    struct PostRender {};

    class PresentationGroup : public SystemGroup {

    public:
        void onCreate(ECS::World& world) override;
    };
}
