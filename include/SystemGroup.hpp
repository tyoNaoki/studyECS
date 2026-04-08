#pragma once
#include "SystemBase.hpp"
#include <queue>
#include <algorithm>

namespace ECS::System{

    struct SystemBase;

    struct SystemGroup : public SystemBase{
        std::vector<SystemBase*> systems;
        std::vector<SystemBase*> sorted;
        bool dirty = true;

        SystemGroup(World& w) : SystemBase(w) {}

        void add(SystemBase* s){
            systems.push_back(s);
            dirty = true;
        };

        void sort(){
            for (auto* sys : systems) {
                if (auto* group = dynamic_cast<SystemGroup*>(sys)) {
                    group->sort();
                }
            }

            topologicalSort();
            dirty = false;
        };

        void  topologicalSort(){
            //入次数を計算
            std::unordered_map<SystemBase*, int> indegree;
            for (auto system : systems) {
                indegree[system] = 0;
            }
            for (auto system : systems) {
                for (auto* b : system->before) {
                    indegree[system]++;
                }
            }

            //入次数0のノードをキューに入れる
            std::queue<SystemBase*> q;
            for (auto& [sys, deg] : indegree) {
                if (deg == 0) q.push(sys);
            }

            sorted.clear();

            //Kahn法
            while (!q.empty()) {
                auto* sys = q.front();
                q.pop();
                sorted.push_back(sys);

                //後に来るべきシステムの入次数を減らす
                for (auto system : systems) {
                    if (std::find(system->after.begin(), system->after.end(), sys) != system->after.end()) {
                        indegree[system]--;
                        if (indegree[system] == 0) {
                            q.push(system);
                        }
                    }
                }
            }
        }

        void update() override{
            if (dirty) sort();

            for(int i=0;i<sorted.size();i++){
                sorted[i]->update();
            }
        };
    };

}