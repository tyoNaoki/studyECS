#pragma once
#include "DxLib.h"
#include <unordered_map>
#include <string>
#include "Engine\ECS\Component\BasicComponent.h"

namespace ECS::AssetManagement {

    class AssetManager
    {
        AssetManager() = default;

        AssetManager(AssetManager&&) = delete;
        AssetManager& operator=(AssetManager&&) = delete;
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

    public:
        static AssetManager& Instance() {
            static AssetManager manager;
            return manager;
        }

        //モデルの読み込み
        int LoadModel(const char* path) {
            auto it = modelCache.find(path);

            if (it != modelCache.end()) {
                return it->second;
            }

            int model = MV1LoadModel(path);
            modelCache[path] = model;
            return model;
        }

        int LoadTexture(const char* path) {
            auto it = textureCache.find(path);

            if (it != textureCache.end()) {
                return it->second;
            }

            int texture = LoadGraph(path);
            textureCache[path] = texture;
            return texture;
        }

        ECS::Component::CubeMesh* LoadCubeMesh() {
            if (!cubeMeshInitialized) {

                const ECS::Component::Vertex base[8] =
                {
                    { VGet(-30,-30,-30), GetColor(255,0,0) },
                    { VGet(30,-30,-30), GetColor(0,255,0) },
                    { VGet(30, 30,-30), GetColor(0,0,255) },
                    { VGet(-30, 30,-30), GetColor(255,255,0) },
                    { VGet(-30,-30, 30), GetColor(0,255,255) },
                    { VGet(30,-30, 30), GetColor(255,0,255) },
                    { VGet(30, 30, 30), GetColor(255,255,255) },
                    { VGet(-30, 30, 30), GetColor(0,0,0) }
                };

                memcpy(cubeMesh.vertices, base, sizeof(base));
                cubeMeshInitialized = true;
            }

            return &cubeMesh;
        }


    private:
        std::unordered_map<std::string, int> modelCache;
        std::unordered_map<std::string, int> textureCache;

        bool cubeMeshInitialized = false;
        ECS::Component::CubeMesh cubeMesh;
    };
} //namesapce ECS::AssetManagement