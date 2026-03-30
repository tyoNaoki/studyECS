#pragma once
#include <iostream>
#include <chrono>
#include <mutex>
#include <vector>
#include <sstream>
#include <iomanip>

namespace ECS::JobSystem{

using time_point = std::chrono::high_resolution_clock::time_point;

inline time_point now() {
	return std::chrono::high_resolution_clock::now();
}

inline int duration(time_point start, time_point end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
        .count();
}

struct JobData { char name; time_point start, end; };

//単一スレッド分のジョブデータからタイムライン文字列を生成
inline std::string createTimeline(
    const std::vector<JobData>& jobs,
    time_point globalStart,
    int globalDuration)
{
    // まず全体を空白で初期化
    std::string timeline(globalDuration, ' ');

    // 各ジョブの [start,end) 区間を文字で塗りつぶす
    for (const auto& data : jobs) {
        int s = duration(globalStart, data.start);
        int e = duration(globalStart, data.end);
        if (s < 0) s = 0;
        if (e > globalDuration) e = globalDuration;
        std::fill(
            timeline.begin() + s,
            timeline.begin() + e,
            data.name);
    }

    return timeline;
}

//スレッドごとにタイムラインを出力する
inline void printTimelines(
    const std::unordered_map<std::thread::id, std::vector<JobData>>& jobDataMap,
    time_point globalStart,
    int globalDuration)
{
    for (auto& [tid, vec] : jobDataMap) {
        std::string timeline = createTimeline(vec, globalStart, globalDuration);
        std::ostringstream ss;
        ss << std::setw(5) << tid;
        std::cout
            << "Thread " << ss.str()
            << " |" << timeline << "| ("
            << vec.size() << " jobs)\n";
    }
}

struct IRecorder {
    using queueIndex = size_t;
    using logs = std::vector<std::string>;
    using RecordHandle = size_t;
    using RecordMap = std::unordered_map<std::thread::id, std::vector<JobData>>;

    virtual RecordHandle recordStart(char name) = 0;
    virtual void recordEnd(RecordHandle handle) = 0;
    virtual void reset() = 0;
    virtual const RecordMap& getDataMap() const= 0;
};

struct TimelineRecorder : public IRecorder{
	std::mutex   mtx;
    RecordMap jobData;

    RecordHandle recordStart(char name) override{
        auto thisId = std::this_thread::get_id();
        auto t = now();
        
        JobData data{name,t,t};

        std::lock_guard<std::mutex> lock(mtx);
        //std::cout << "[START] Job=" << jobData[thisId].size() << " T=" << thisId << std::endl;
        jobData[thisId].push_back(data);

        return jobData[thisId].size() - 1;
    }

    //最後に追加された要素を更新
	void recordEnd(RecordHandle handle)override {
        auto thisId = std::this_thread::get_id();
        auto t = now();

        std::lock_guard<std::mutex> lock(mtx);

        //std::cout << "[END] Job=" << handle << " T=" << thisId << std::endl;
        auto it = jobData.find(thisId);
        if (it == jobData.end() || it->second.empty())
            return;

        // 末尾要素に終了時刻をセット
        if(handle < it->second.size()){
            it->second.back().end = t;
        }
    }

    void reset() override{
        std::lock_guard<std::mutex> lk(mtx);
        jobData.clear();
    }

    const RecordMap& getDataMap() const override { return jobData; }
};

struct JobScope {
    std::unique_ptr<IRecorder> recorder;           // 記録先
    char      name;               // ジョブ識別用
    std::thread::id threadId;     // どのスレッドか
    time_point startTime;         // 開始時刻

    JobScope(std::unique_ptr<IRecorder>&& rec, char n)
        : recorder(std::move(rec)), name(n),
        threadId(std::this_thread::get_id()),
        startTime(now())
    {
        recorder->recordStart(name);
    }

    ~JobScope() {
        auto endTime = now();
        recorder->recordEnd(name);
    }

    // コピー／ムーブ禁止
    JobScope(const JobScope&) = delete;
    JobScope& operator=(const JobScope&) = delete;
};
}//namespace ECS::Job