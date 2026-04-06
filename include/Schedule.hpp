#pragma once
#include "SystemGroup.hpp"

namespace ECS::Schedule {

    struct SystemHandle {
        uint32_t systemIndex;
        uint32_t systemVersion;

        SystemHandle(uint32_t index, uint32_t version) : systemIndex(index), systemVersion(version){}
        SystemHandle():systemIndex(NULL_SYSTEM_INDEX),systemVersion(0){}
    };

    
    inline bool isSystemValid(SystemHandle& handle) {
        return handle.systemIndex != 0xFFFFFFFFu; // ç≈ëÂílÇ∆î‰ärÇ∑ÇÈ
    }

    inline uint32_t NULL_SYSTEM_INDEX = 0xFFFFFFFFu;

    enum class SystemGroupTag {
        initialization,
        simulation,
        presentation
    };

struct Schedule {
    ECS::System::SystemGroup initialization;
    ECS::System::SystemGroup simulation;
    ECS::System::SystemGroup presentation;

    void add(SystemGroupTag tag,SystemHandle systemHandle){
        switch (tag)
        {
        case ECS::Schedule::SystemGroupTag::initialization:
            //initialization.add()
            break;
        case ECS::Schedule::SystemGroupTag::simulation:
            //simulation.add()
            break;
        case ECS::Schedule::SystemGroupTag::presentation:
            //presentation.add()
            break;
        default:
            break;
        }
    }

    void sort(){
        initialization.sort();
        simulation.sort();
        presentation.sort();
    }

    SystemHandle update(){
        SystemHandle handle;

        handle = initialization.update(handle);
        handle = simulation.update(handle);
        handle = presentation.update(handle);
    };
};

}
