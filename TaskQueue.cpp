#include "TaskQueue.h"
#include "JobManager.h"

namespace ECS::JobSystem{

    void TaskArena::Initialize(JobCategory category,uint8_t maxSlotWorkCap, size_t maxTasks, size_t chunkSize)
{
    jobCategory = category;
    maxWorkloadOfOneSlot = maxSlotWorkCap;
    chunkMaxSize = chunkSize;
    maxTaskCapacity = maxTasks;
    currentChunk = jobCategory;

    data.reserve(maxTasks);

    for (int i = 0; i < static_cast<int>(TaskCategory::Num); i++) {
        auto cat = static_cast<TaskCategory>(i);
        currentSlots[cat].reserve(maxWorkloadOfOneSlot / getWorkload(cat));
    }

    initFlag = true;
}

void TaskArena::flushIncomplete(){
    if(emptyFlag) return;

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

    emptyFlag = true;
}

void TaskArena::enqueue(TaskCategory cat, JobId jobId){
    uint8_t workload = getWorkload(cat);

    std::lock_guard<std::mutex> guard(lock);

  //一つのスロット上限値を追加前から超えていたら、そのままdataにpush
    if (workload >= maxWorkloadOfOneSlot) {
        pushJob(cat, std::move(jobId));
        return;
    }

    currentSlots[cat].push_back(std::move(jobId));

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

void TaskArena::enqueue(TaskCategory cat, std::vector<JobId>&& jobIds) {
    uint8_t workload = getWorkload(cat);

    std::lock_guard<std::mutex> guard(lock);

    currentSlots[cat].insert(currentSlots[cat].end(), 
        std::make_move_iterator(jobIds.begin()),
        std::make_move_iterator(jobIds.end()));

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

            currentSlots[cat].clear();

            return;
        }

        uint8_t workload = getWorkload(cat);
        size_t slotCapacity = maxWorkloadOfOneSlot / workload;

        //slotCapacityを超えている場合は分割
        while (currentSlots[cat].size() >= slotCapacity) {

            //slotCapacity分だけ切り出す
            std::vector<JobId> fullSlot;
            fullSlot.reserve(slotCapacity);

            auto begin = currentSlots[cat].begin();
            auto mid = begin + slotCapacity;

            std::move(begin, mid, std::back_inserter(fullSlot));

            //残りをcurrentSlotsに残す
            currentSlots[cat].erase(begin, mid);

            pushJobs(cat, std::move(fullSlot));
        }
    }
}

}