#include "Engine\ECS\System\SystemGroup.h"
#include "Engine\ECS\World.h"

void ECS::System::SystemGroup::onUpdate(ECS::World& world){
    if (dirty) sort(world);

    for (int i = 0; i < sorted.size(); i++) {
        auto&entry = world.getSystemEntry(sorted[i]);
        if(entry.isGroup){
            auto ptr = world.getSystemGroup(entry.index);
            ptr->onUpdate(world);
        }else{
            auto ptr = world.getSystem(entry.index);
            ptr->onUpdate(world);
        }
    }
}

void ECS::System::SystemGroup::topologicalSort(ECS::World& world, SystemGroup& group)
{
    auto& handles = group.systems;
    const size_t N = handles.size();

    group.sorted.clear();
    group.sorted.reserve(N);

    if (N == 0) {
        group.dirty = false;
        return;
    }

    //入次数テーブル
    std::vector<int> indegree(N, 0);

    //id → index
    std::unordered_map<size_t, size_t> indexOf;
    indexOf.reserve(N);
    for (size_t i = 0; i < N; i++)
        indexOf[handles[i].ID] = i;

    //beforeから入次数を計算
    for (size_t i = 0; i < N; i++) {
        SystemHandle handle = handles[i];

        //入次数を足していく
        for (SystemHandle before : world.getSystemEntry(handle).before) {
            auto it = indexOf.find(before.ID);
            if (it != indexOf.end()) {
                indegree[i]++;
            }
        }
    }

    //入次数0をキューに入れる
    std::queue<size_t> q;
    for (size_t i = 0; i < N; i++)
        if (indegree[i] == 0)
            q.push(i);

    //ソート
    while (!q.empty()) {
        size_t idx = q.front();
        q.pop();

        SystemHandle handle = handles[idx];
        group.sorted.push_back(handle);

        //afterのみ減らす
        for (SystemHandle after : world.getSystemEntry(handle).after) {
            auto it = indexOf.find(after.ID);
            if (it != indexOf.end()) {
                size_t j = it->second;
                indegree[j]--;
                if (indegree[j] == 0)
                    q.push(j);
            }
        }
    }

    //ソート失敗
    if (group.sorted.size() != N) {
        throw std::runtime_error("Cyclic dependency detected in SystemGroup");
        ASSERT(false, "Cyclic dependency detected in SystemGroup");
    }
}
