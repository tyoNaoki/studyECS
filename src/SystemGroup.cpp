#include "SystemGroup.h"
#include "World.h"

void ECS::System::SystemGroup::onUpdate(ECS::World& world){
    if (dirty) sort(world);

    for (int i = 0; i < sorted.size(); i++) {
        auto& entry = world.getSystem(sorted[i]);
        if (entry.fn) {
            entry.fn(world);
        }else{
            world.getSystemGroup(entry.groupID)->onUpdate(world);
        }
    }
}

void ECS::System::SystemGroup::topologicalSort(ECS::World& world, SystemGroup& group)
{
    auto& ids = group.systems;
    const size_t N = ids.size();

    group.sorted.clear();
    group.sorted.reserve(N);

    if (N == 0) {
        group.dirty = false;
        return;
    }

    // --- 1. 入次数テーブル ---
    std::vector<int> indegree(N, 0);

    // id → index
    std::unordered_map<SystemID, size_t> indexOf;
    indexOf.reserve(N);
    for (size_t i = 0; i < N; i++)
        indexOf[ids[i]] = i;

    // --- 2. before / after から入次数を計算 ---
    for (size_t i = 0; i < N; i++) {
        SystemID id = ids[i];

        // before: before → id
        for (SystemID before : world.getSystem(id).before) {
            auto it = indexOf.find(before);
            if (it != indexOf.end()) {
                indegree[i]++;
            }
        }
    }

    // --- 3. 入次数 0 をキューへ ---
    std::queue<size_t> q;
    for (size_t i = 0; i < N; i++)
        if (indegree[i] == 0)
            q.push(i);

    // --- 4. Kahn 法 ---
    while (!q.empty()) {
        size_t idx = q.front();
        q.pop();

        SystemID id = ids[idx];
        group.sorted.push_back(id);

        // after のみ削除（id → after）
        for (SystemID after : world.getSystem(id).after) {
            auto it = indexOf.find(after);
            if (it != indexOf.end()) {
                size_t j = it->second;
                indegree[j]--;
                if (indegree[j] == 0)
                    q.push(j);
            }
        }
    }

    // --- 5. サイクル検出 ---
    if (group.sorted.size() != N) {
        throw std::runtime_error("Cyclic dependency detected in SystemGroup");
        ASSERT(false, "Cyclic dependency detected in SystemGroup");
    }
}
