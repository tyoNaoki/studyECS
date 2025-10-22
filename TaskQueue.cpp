#include "TaskQueue.h"
#include "JobManager.h"

namespace ECS::JobSystem{

TaskArena::TaskArena(JobCategory category,uint8_t maxSlotWorkCap, size_t maxTasks, size_t chunkSize) : 
jobCategory(category),maxWorkloadOfOneSlot(maxSlotWorkCap), chunkMaxSize(chunkSize), maxTaskCapacity(maxTasks),currentChunk(jobCategory) {

    data.reserve(maxTasks);

    for (int i = 0; i < static_cast<int>(TaskCategory::Num); i++) {
        auto cat = static_cast<TaskCategory>(i);
        currentSlots[cat].reserve(maxWorkloadOfOneSlot / getWorkload(cat));
    }
}

void TaskArena::flushIncomplete(){
    std::lock_guard<std::mutex> guard(lock);
    //現在の未完成スロットすべて積む
    for (auto& [cat, slot] : currentSlots) {
        if (!slot.empty()) {
            pushJobs(cat, std::move(slot));
            slot.clear();
        }
    }

    //未完成のchunkも空でなければ積む
    if (!currentChunk.isEmpty()) {
        pushChunk(std::move(currentChunk));

        currentChunk = ChunkMeta(jobCategory);
    }
}

void TaskArena::enqueue(TaskCategory cat,JobHandle handle){
    uint8_t workload = getWorkload(cat);

    std::lock_guard<std::mutex> guard(lock);

    //一つのスロット上限値を追加前から超えていたら、そのままdataにpush
    if (workload >= maxWorkloadOfOneSlot) {
        pushJob(cat, std::move(handle));
        return;
    }

    currentSlots[cat].push_back(std::move(handle));

    //一つslotが最大値に達したら
    if (isFullSlot(cat)) {

        //スロットのJobsの追加でストレージ容量を超える時
        if (data.size() + currentSlots[cat].size() > maxTaskCapacity) {

            //その場で実行
            auto& jm = JobManager::Instance();
#ifdef DEBUG
            ASSERT(false, "Job capacity exceeded!");
#else
            jm.executor().runSlot(99, &*currentSlots[cat].begin(), &*currentSlots[cat].end());
#endif

        }
        pushJobs(cat, std::move(currentSlots[cat]));

        currentSlots[cat].clear();
        return;
    }
}

}