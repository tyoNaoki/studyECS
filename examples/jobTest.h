#pragma once
#include "Engine\Core\JobManager.h"
#include "TestFramework.hpp"

int test()
{
	RUN_TEST("test_jobSystem", 1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

void busyWait(std::chrono::nanoseconds duration) {
	auto start_time = std::chrono::high_resolution_clock::now();
	while (std::chrono::high_resolution_clock::now() - start_time < duration) {
		// CPU時間を消費するためのアクティブな待機
		// ループの中で何もしない
	}
}

struct TestHoge
{
	int value = 2;

	TestHoge(int v) : value(v) {};
};


struct TestPrintJob
	: public ECS::JobSystem::IJob<TestPrintJob>
{
	TestHoge hoge;

	/*static void Execute(TestJob*job) {
		job->connecter.value = job->connecter.value * job->connecter.value;

	}*/

	TestPrintJob(int value) : hoge(value) {
	}

	void execute() {
		hoge.value = hoge.value * hoge.value;
	}
};

struct TestJob
	: public ECS::JobSystem::IJob<TestJob>
{
	TestHoge hoge;

	/*static void Execute(TestJob*job) {
		job->connecter.value = job->connecter.value * job->connecter.value;

	}*/

	TestJob(int value) : hoge(value) {
	}

	void execute() {
		hoge.value = hoge.value * hoge.value;
	}
};

struct TestParticalJob
	: public ECS::JobSystem::IJob<TestParticalJob>
{
	std::string name;

	/*static void Execute(TestJob*job) {
		job->connecter.value = job->connecter.value * job->connecter.value;

	}*/

	TestParticalJob(std::string n) : name(n) {}

	void execute() {
		std::printf("%s particalJob executed \n", name.c_str());
	}
};

struct TestDelayParticalJob
	: public ECS::JobSystem::IJob<TestDelayParticalJob>
{
	std::string name;
	int delayTime;

	/*static void Execute(TestJob*job) {
		job->connecter.value = job->connecter.value * job->connecter.value;

	}*/

	TestDelayParticalJob(std::string n, int delay) : name(n), delayTime(delay) {}

	void execute() {
		std::printf("%s particalJob executed \n", name.c_str());
		busyWait(std::chrono::milliseconds(delayTime));
	}
};

TEST_CASE_PRIORITY(test_jobSystem) {
	int check = 9'0000;

	auto recorder = std::make_unique<ECS::JobSystem::TimelineRecorder>();
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(9'0000, 7, std::move(recorder));

	//int check = 5625;
	
	
	//バッチ処理
	// check 個のジョブをスケジュール
	for (int i = 0; i < check; ++i) {
		auto job = TestJob::create(i);

		auto handle = job->scheduleIJob(ECS::JobSystem::TaskCategory::Batch);
	}

	std::printf("RealTimeJob Start is %zu\n", jm.getStats().scheduledJobCount());

	auto globalStart = ECS::JobSystem::now();
	jm.start();

	jm.getStats().waitForAll();

	auto globalEnd = ECS::JobSystem::now();

	std::printf("realTime job %zu finished!! \n", check);

	int  globalDuration = ECS::JobSystem::duration(globalStart, globalEnd);
	std::cout << "Total duration: "
		<< globalDuration << " ms\n";
}

TEST_CASE_PRIORITY(test_chainJobSystem) {

	auto recorder = std::make_unique<ECS::JobSystem::TimelineRecorder>();
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(100, 7, std::move(recorder));

	jm.start();

	//単一依存
	/*A → B
	B は A 完了後に実行されることを確認。*/
	{
		auto jobA = TestParticalJob::create("A");
		auto handle = jobA->scheduleIJob();

		auto jobB = TestParticalJob::create("B");
		auto handle2 = jobB->scheduleIJob(handle);
	}

	jm.getStats().waitForAll();

	/*複数依存(fan - in)
		A, B → C*/
	{
		std::vector<ECS::JobSystem::JobHandle>handles;

		auto jobA = TestParticalJob::create("A");
		auto handle = jobA->scheduleIJob();

		auto jobB = TestParticalJob::create("B");
		auto handle2 = jobB->scheduleIJob();

		handles.push_back(handle);
		handles.push_back(handle2);

		auto jobC = TestParticalJob::create("C");
		auto handle3 = jobC->scheduleIJob(handles);

		// 少しずつずらしてスケジューリング
		busyWait(std::chrono::milliseconds(2));
	}

	jm.getStats().waitForAll();

	/*複数子依存 (fan-out)
		A,B → C, D。*/
	{

		std::vector<ECS::JobSystem::JobHandle>handles;

		auto jobA = TestDelayParticalJob::create("A", 4);
		auto handle = jobA->scheduleIJob();

		auto jobB = TestDelayParticalJob::create("B", 4);
		auto handle2 = jobB->scheduleIJob();

		handles.push_back(handle);
		handles.push_back(handle2);

		auto jobC = TestParticalJob::create("C");
		auto handle3 = jobC->scheduleIJob(handles);

		auto jobD = TestParticalJob::create("D");
		auto handle4 = jobD->scheduleIJob(handles);

		// 少しずつずらしてスケジューリング
		busyWait(std::chrono::milliseconds(2));
	}

	jm.getStats().waitForAll();

	/*深い依存チェーン
		A → B → C → D
		順序通りに実行されることを確認。*/
	{
		auto jobA = TestParticalJob::create("A");
		auto handle = jobA->scheduleIJob();

		auto jobB = TestDelayParticalJob::create("B", 2);
		auto handle2 = jobB->scheduleIJob(handle);

		auto jobC = TestParticalJob::create("C");
		auto handle3 = jobC->scheduleIJob(handle2);

		auto jobD = TestParticalJob::create("D");
		auto handle4 = jobD->scheduleIJob(handle3);

		// 少しずつずらしてスケジューリング
		busyWait(std::chrono::milliseconds(2));
	}

	jm.getStats().waitForAll();
}

struct TestParallelJob
	: public ECS::JobSystem::IParallelJob<TestParallelJob>
{
	std::vector<int>results;

	TestParallelJob(size_t size) : results(size, 0) {};

	inline void Execute(size_t index) {
		//results[index] = index * index;
		std::printf("%zu is result is %d \n", index, results[index]);
	}


};

struct TestPrintParallelJob
	: public ECS::JobSystem::IParallelJob<TestPrintParallelJob>
{
	std::vector<int>results;
	std::string name;

	TestPrintParallelJob(size_t size, std::string n) : results(size, 0), name(n) {};

	inline void Execute(size_t index) {
		std::printf("%s TestPrintParallelJob[%zu] Executed \n", name.c_str(), index);
	}

	~TestPrintParallelJob() {
		std::printf("%s deleted \n", name.c_str());
	}
};

//パラレルジョブテスト
TEST_CASE_ORDER(test_bigJobSystem) {
	auto recorder = std::make_unique<ECS::JobSystem::TimelineRecorder>();
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(100, 7, std::move(recorder));

	/*size_t resultSize = 100;

	size_t batchSize = 10;

	size_t workerCount = 2;*/

	size_t resultSize = 5;

	size_t batchSize = 1;

	size_t workerCount = 2;

	/*size_t resultSize = 10'0000;

	size_t batchSize = 1000;

	size_t workerCount = 7;*/
	{
		auto parallelJob = TestPrintParallelJob::create(resultSize, "A");

		auto handle = parallelJob->schedule(resultSize, batchSize, workerCount);
	}

	std::printf("ParallelJob %zu Start is %d\n", jm.getStats().scheduledJobCount(), resultSize / batchSize);

	auto globalStart = ECS::JobSystem::now();

	jm.start();

	jm.getStats().waitForAll();

	// 5) テスト終了時刻を記録＆全体持続時間を計算
	auto globalEnd = ECS::JobSystem::now();
	int  globalDuration = ECS::JobSystem::duration(globalStart, globalEnd);
	std::cout << "Total duration: "
		<< globalDuration << " ms\n";
}

//パラレルジョブに依存関係追加
TEST_CASE_ORDER(test_chainBigJobSystem) {
	auto recorder = std::make_unique<ECS::JobSystem::TimelineRecorder>();
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(100, 7, std::move(recorder));

	size_t resultSize = 5;

	size_t batchSize = 1;

	size_t workerCount = 2;

	jm.start();

	/*複数依存(fan - in)
		A → B*/
	{
		auto testIJob = TestDelayParticalJob::create("A", 2);

		auto handle = testIJob->scheduleIJob();

		auto parallelJob = TestPrintParallelJob::create(resultSize, "B");

		parallelJob->schedule(resultSize, batchSize, workerCount, handle);
	}

	jm.getStats().waitForAll();

	/*複数依存(fan - in)
		A, B → C*/
	{
		auto testIJob = TestDelayParticalJob::create("A", 2);

		auto handle = testIJob->scheduleIJob();

		auto testIJob2 = TestDelayParticalJob::create("B", 4);

		auto handle2 = testIJob2->scheduleIJob();

		std::vector<ECS::JobSystem::JobHandle>handles;
		handles.push_back(handle);
		handles.push_back(handle2);

		auto parallelJob = TestPrintParallelJob::create(resultSize, "C");

		parallelJob->schedule(resultSize, batchSize, workerCount, handles);
	}

	jm.getStats().waitForAll();

	/*複数依存(fan - in)
		A → B*/
	{
		auto parallelJob = TestPrintParallelJob::create(resultSize, "A");

		auto handle = parallelJob->schedule(resultSize, batchSize, workerCount);

		auto parallelJob2 = TestPrintParallelJob::create(resultSize, "B");

		parallelJob2->schedule(resultSize, batchSize, workerCount, handle);
	}

	jm.getStats().waitForAll();
}
