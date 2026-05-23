#pragma once


namespace ECS {
    class World;
    namespace System{

        struct SystemScheduler {
            void initialize(World& world);

            void onUpdate(World& world);
            void onRender(World& world);
            void onCleanup(World& world);

        private:
            size_t initializationIndex;
            size_t simulationIndex;
            size_t presentationIndex;
            size_t cleanupIndex;
        };
    }
}
