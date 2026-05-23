#pragma once
#undef min

#include <utility> 
#include <algorithm>
#include <cstddef>   // std::nullptr_t
#include <variant>
#include <memory>
#include <array>
#include "TestFramework.hpp"
#include "Engine/Containers/HashFunctions.hpp"
#include <optional>

namespace ECS::JobSystem{
    
    using JobIndex = uint32_t;
    using JobVersion = uint32_t;
    using JobId = uint64_t;

    inline JobId composeJobId(JobIndex index, JobVersion version) {
        return (static_cast<JobId>(index) << 32) | static_cast<JobId>(version);
    }

    inline JobIndex getJobIndex(JobId id) {
        return id >> 32;//上位32ビットを取得
    }

    inline std::string jobInfo(JobId id) {
        return "[JOBID : '" + std::to_string(getJobIndex(id)) + "]";
    }

    inline JobVersion getJobVersion(JobId id) {
        return static_cast<JobVersion>(id & 0xFFFFFFFF); // 下位32ビットを取得
    }

    inline bool isEntityValid(JobId id) {
        return getJobIndex(id) != 0xFFFFFFFF; // 最大値と比較する
    }

    inline JobIndex NULL_JOB_INDEX = 0xFFFFFFFFu;
    inline JobId NULL_JOB_ID = composeJobId(0xFFFFFFFFu, 0u);

    //TaskCategoryをparallelJobに変更
    enum class TaskCategory : uint8_t {
        Normal = 0,
        Batch = 1,
        Parallel = 2,
        Num = 3,
    };

#define REFLECT_FIELDS(Type, ...)                                  \
  static constexpr auto field_ptrs()                                \
  { return std::make_tuple(__VA_ARGS__); }

struct Job {

private:
    // 最大キャプチャ領域
    static constexpr size_t BufferSize = 32;

    // 呼び出し時の関数ポインタ型
    using Invoker = void(*)(void*,const size_t);
    using Destroyer = void(*)(void*);

    // 実データ格納＋呼び出し子
    alignas(void*) char  buf[BufferSize];
    Invoker invoke_fn = nullptr;
    Destroyer  destroy_fn = nullptr;

public:
    Job() = default;

    Job(Job&& o) noexcept {
        invoke_fn = o.invoke_fn;
        destroy_fn = o.destroy_fn;
        memcpy(buf, o.buf, BufferSize);
        o.invoke_fn = nullptr;
        o.destroy_fn = nullptr;
    }

    // 任意の小さいラムダ／関数オブジェクトをムーブキャプチャ
    template<typename F>
    Job(F&& f) noexcept {
        static_assert(sizeof(F) <= BufferSize,
            "Job function over BufferSize");
        new (buf) F(std::move(f));
        invoke_fn = [](void* p,const size_t id) {
            auto fp = static_cast<F*>(p);
            (*fp)(id);
        };

        destroy_fn = [](void* p) {
            static_cast<F*>(p)->~F();
        };
    }

    //デストラクター
    ~Job() {
        if (destroy_fn) {
            destroy_fn(buf);
        }
    }

    //ここで必要になるのがムーブ代入演算子
    Job& operator=(Job&& o) noexcept {
        if (this != &o) {
            // 破棄
            if (destroy_fn) destroy_fn(buf);

            //move
            invoke_fn = o.invoke_fn;
            destroy_fn = o.destroy_fn;
            std::memcpy(buf, o.buf, BufferSize);

            //クリア
            o.invoke_fn = nullptr;
            o.destroy_fn = nullptr;
        }
        return *this;
    }

    // 一度きりの実行
    void invoke(const size_t id) noexcept {
        if (invoke_fn) {
            invoke_fn(buf,id);
            if (destroy_fn) {
                destroy_fn(buf);
                destroy_fn = nullptr;
            }

            invoke_fn = nullptr;
        }
    }

    bool valid() const noexcept {
        return invoke_fn != nullptr;
    }

    // 暗黙の bool 変換は禁止
    explicit operator bool() const noexcept = delete;
};
struct FutureInner {
    FutureInner() = default;

    // コピー禁止
    FutureInner(const FutureInner&) = delete;
    FutureInner& operator=(const FutureInner&) = delete;

    // ムーブ禁止
    FutureInner(FutureInner&&) = delete;
    FutureInner& operator=(FutureInner&&) = delete;

    std::atomic<bool> ready{ false };
};

class JobFuture {
    std::shared_ptr<FutureInner> inner;
public:
    explicit JobFuture(std::shared_ptr<FutureInner> i)
        : inner(std::move(i)) {}

    //JobSystem::run_one_pending_job() を呼びつつ待ち
    bool isReady() {
        return inner->ready.load(std::memory_order_acquire);
    }
};

class SettableJobFuture {
    std::shared_ptr<FutureInner> inner;
public:
    explicit SettableJobFuture(std::shared_ptr<FutureInner> i)
        : inner(std::move(i)) {}

    static auto create() {
        auto ptr = std::make_shared<FutureInner>();
        return std::make_pair(
            SettableJobFuture{ ptr },
            JobFuture{ptr}
        );
    }

    // 結果なしの通知だけ
    void set_value() {
        inner->ready.store(true, std::memory_order_release);;
    }
};

struct ChunkMeta {
    std::vector<Job> jobs;

    size_t size() {
        return jobs.size();
    }

    bool isEmpty() {
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

template<typename>
struct IJob;

class JobManager;

struct Inner {
    Inner() = default;

    // コピー禁止
    Inner(const Inner&) = delete;
    Inner& operator=(const Inner&) = delete;

    // ムーブ禁止
    Inner(Inner&&) = delete;
    Inner& operator=(Inner&&) = delete;

    void setReady(bool r) {
        ready.store(r, std::memory_order_release);
    }

    bool isReady() const {
        return ready.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> ready{ false };
};

struct JobHandle {
    bool isComplete() const{
        return inner->isReady();
    };

    void Complete() const;

    JobId getJobId() { return jobId; }

    // 外部には見せない
    static JobHandle createHandle(JobId id, std::shared_ptr<Inner>&& inner) {
        JobHandle h;
        h.jobId = id;
        h.inner = std::move(inner);
        return h;
    }

private:
    JobId jobId;
    std::shared_ptr<Inner>inner;

    // デフォルトコンストラクタ
    JobHandle() = default;
};

template<typename Derived>
struct IJob : public std::enable_shared_from_this<Derived>{

protected:
    IJob() = default;

public:
    virtual ~IJob() = default;

    template<typename... Args>
    static std::shared_ptr<Derived> create(Args&&... args){
        return std::make_shared<Derived>(std::forward<Args>(args)...);
    }

    void addRequest(std::function<void(Derived&)>&& fn) {
        commands.emplace_back(std::move(fn));
    }

    void addRequest(std::vector<std::function<void(Derived&)>>&& fns) {
        std::swap(commands,fns);
    }

    JobHandle scheduleIJob(TaskCategory taskCategory = TaskCategory::Normal) {

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleJobHandle(self, taskCategory);
    }

    JobHandle scheduleIJob(JobHandle& dependentHandle,TaskCategory taskCategory = TaskCategory::Normal) {

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleJobHandle(self, taskCategory, dependentHandle);
    }

    JobHandle scheduleIJob(std::vector<JobHandle>& dependentHandles, TaskCategory taskCategory = TaskCategory::Normal){
        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleJobHandle(self, taskCategory, dependentHandles);
    }

    inline void execute() {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute() member function!!");
    }

private:
    void applyCommands() {

        if (commands.empty()) return;

        for (auto& fn : commands) {
            fn(static_cast<Derived&>(*this)); // 直接呼び出し
        }

        commands.clear();
    }

    std::shared_ptr<Derived> shared_this(){
        return std::enable_shared_from_this<Derived>::shared_from_this();
    }

private:
    // コマンドバッファ
    std::vector<std::function<void(Derived&)>> commands;
};

//parallelJobの基底クラス
//Derived : 派生クラス
//ParallelJoData : Job実行時に書き込むデータ
template<typename Derived>
struct IParallelJob : public std::enable_shared_from_this<Derived>{

private:
    

protected:
    IParallelJob() = default;
    
public:
    virtual ~IParallelJob(){};

    template<typename... Args>
    static std::shared_ptr<Derived> create(Args&&... args) {
        return std::make_shared<Derived>(std::forward<Args>(args)...);
    }

    JobHandle schedule(const size_t total,const size_t batchSize,const size_t workerCount) {
        ASSERT(total > 0 && batchSize > 0, "parallelJob schedule total or batchSize is zero");

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleParalellJobHandle(self,total,batchSize,workerCount);
    }

    JobHandle schedule(const size_t total, const size_t batchSize, const size_t workerCount,JobHandle& dependentHandle) {
        ASSERT(total > 0 && batchSize > 0, "parallelJob schedule total or batchSize is zero");

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleParalellJobHandle(self,total, batchSize, workerCount, dependentHandle);
    }

    JobHandle schedule(const size_t total, const size_t batchSize, const size_t workerCount, std::vector<JobHandle>& dependentHandles) {
        ASSERT(total > 0 && batchSize > 0, "parallelJob schedule total or batchSize is zero");

        std::shared_ptr<Derived> self = shared_this();
        auto& jm = JobManager::Instance();

        return jm.scheduleParalellJobHandle(self, total, batchSize, workerCount, dependentHandles);
    }

    void AddRequeset(std::function<void(Derived&)>&& fn) {
        commands.emplace_back(std::move(fn));
    }

    inline void Execute(size_t index) {
        // Derived の Execute() を呼び出し
        //static_cast<Derived*>(this)->Execute(index);
        ASSERT(false, "Derived class not found Execute(size_t) member function!!");
    }

    inline void ExecuteBatch(const size_t start, const size_t len, Derived* self) {
        for (size_t i = start; i < start + len; ++i) {
            self->Execute(i);
        }
    }
   
private:
    void applyCommands() {

        if (commands.empty()) return;

        for (auto& fn : commands) {
            fn(static_cast<Derived&>(*this)); // 直接呼び出し
        }

        commands.clear();
    }

protected:
    std::shared_ptr<Derived> shared_this()
    {
        return std::enable_shared_from_this<Derived>::shared_from_this();
    }

private:
    
    // コマンドバッファ
    std::vector<std::function<void(Derived&)>> commands;
};

}//namespace ECS::JobSystem
