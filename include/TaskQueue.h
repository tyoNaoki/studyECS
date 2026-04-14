#pragma once
#include "taskPtr.hpp"
#include <deque>

namespace ECS::JobSystem{


struct ChunkMeta {
    std::vector<Job> jobs;

    size_t size(){
        return jobs.size();
    }

    bool isEmpty(){
        return size() == 0;
    }

    ChunkMeta() = default;

    // コピー禁止
    ChunkMeta(const ChunkMeta&) = delete;
    ChunkMeta& operator=(const ChunkMeta&) = delete;

    // move のみ許可
    ChunkMeta(ChunkMeta&& other) noexcept
        : jobs(std::move(other.jobs))
    {}

    ChunkMeta& operator=(ChunkMeta&& other) noexcept {
        if (this != &other) {
            jobs = std::move(other.jobs);
        }
        return *this;
    }
};

class JobManager;

//Chunkはリストのブロック
//class TaskArena {
//    TaskArena() = delete;
//
//public:
//    TaskArena(JobCategory jobCat,size_t maxBatch) : jobCategory(jobCat), batchMaxSize(maxBatch){
//        currentBatchChunk.jobs.reserve(maxBatch);
//        currentBatchChunk.jobCategory = jobCat;
//    };
//
//     void enqueue(ChunkMeta&&chunk){
//        std::lock_guard<std::mutex> guard(lock);
//
//        chunks.push_back(std::move(chunk));
//     }
//
//     //Batch処理用の関数
//    void enqueue(Job&&job){
//        currentBatchChunk.jobs.push_back(std::move(job));
//
//        if (currentBatchChunk.size() >= batchMaxSize) {
//            enqueue(std::move(currentBatchChunk));
//
//            currentBatchChunk.jobCategory = jobCategory;
//            currentBatchChunk.jobs.reserve(batchMaxSize);
//        }
//    }
//
//    //chunkが一つもない
//    bool isEmptyChunks() const noexcept{
//        return 
//            chunks.size() == 0;
//    }
//
//    //一つchunkPop
//    bool popOne(ChunkMeta& chunk) {
//        std::lock_guard lk(lock);
//        
//        if(isEmptyChunks()){
//            return false;
//        }
//        
//        chunk = std::move(chunks.front());
//        chunks.pop_front();
//
//        return true;
//    }
//
//    //chunkをpop
//    void popMany(size_t maxCount,std::vector<ChunkMeta>&out){
//
//        //取得スロットが空かchunksが空
//        if(maxCount == 0||emptyFlag) return;
//
//        //現在の満タンのchunkがない場合、代わりに未完成のchunkを積む
//        if (isEmptyChunks()) {
//            return;
//        }
//    
//        out.reserve(std::min(maxCount, chunks.size()));
//
//        size_t budget = maxCount;
//    
//        {
//            std::lock_guard<std::mutex> guard(lock);
//
//            while (!isEmptyChunks() && budget > 0) {
//
//                out.push_back(std::move(chunks.front()));
//                chunks.pop_front();
//                budget--;
//            }
//        }
//    }
//
//    bool getFlushBatchChunk(ChunkMeta&chunk){
//        if(currentBatchChunk.isEmpty()) return false;
//
//        chunk = std::move(currentBatchChunk);
//
//        currentBatchChunk.jobCategory = jobCategory;
//        currentBatchChunk.jobs.reserve(batchMaxSize);
//        return true;
//    }
//
//private:
//    std::deque<ChunkMeta> chunks;
//
//    std::mutex lock;
//
//    JobCategory jobCategory;
//
//    bool emptyFlag = false;
//
//    //Batch処理用のchunk詰め変数
//    ChunkMeta currentBatchChunk;
//    size_t batchMaxSize;
//};

}