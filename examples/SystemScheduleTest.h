#pragma once
#include "TestFramework.hpp"
#include "Engine\ECS\World.h"

int system_Test()
{
	RUN_TEST("test_SystemSchedule", 1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

class TestSystemA : ECS::System::SystemBase{
public:
	std::string name;

	void onCreate(ECS::World& world) override{
		name = "A";
	}
	void onUpdate(ECS::World& world) override{
		std::printf("%s is updated\n",name.c_str());
	}
};

class TestSystemB : ECS::System::SystemBase {
public:
	std::string name;

	void onCreate(ECS::World& world) override {
		name = "B";
	}

	void onUpdate(ECS::World& world) override {
		std::printf("%s is updated\n", name.c_str());
	}
};

class TestSystemC : ECS::System::SystemBase {
public:
	std::string name;

	void onCreate(ECS::World& world) override {
		name = "C";
	}

	void onUpdate(ECS::World& world) override {
		std::printf("%s is updated\n", name.c_str());
	}
};

class TestSystemD : ECS::System::SystemBase {
public:
	std::string name;

	void onCreate(ECS::World& world) override {
		name = "D";
	}

	void onUpdate(ECS::World& world) override {
		std::printf("%s is updated\n", name.c_str());
	}
};

class TestSystemE : ECS::System::SystemBase {
public:
	std::string name;

	void onCreate(ECS::World& world) override {
		name = "E";
	}

	void onUpdate(ECS::World& world) override {
		std::printf("%s is updated\n", name.c_str());
	}
};

class TestSystemF : ECS::System::SystemBase {

public:
	std::string name;

	void onCreate(ECS::World& world) override {
		name = "F";
	}

	void onUpdate(ECS::World& world) override {
		std::printf("%s is updated\n", name.c_str());
	}
};

namespace ECS::System {
	struct PreInitialization {};
	struct Initialization {};
}

TEST_CASE_PRIORITY(test_SystemSchedule) {
	ECS::World world;

	world = ECS::World();

	world.initialize();

	auto A = world.registerSystem<TestSystemA,ECS::System::PreInitialization>();
	auto C = world.registerSystem<TestSystemC,ECS::System::PreInitialization>();
	auto B = world.registerSystem<TestSystemB, ECS::System::PreInitialization>();

	auto F = world.registerSystem<TestSystemF, ECS::System::PreInitialization>();
	auto D = world.registerSystem<TestSystemD, ECS::System::PreInitialization>();
	auto E = world.registerSystem<TestSystemE, ECS::System::PreInitialization>();

	// A → B
	world.addBefore(A, B);

	//// B → C
	world.addAfter(B, C);
	//// C → D
	world.addBefore(C, D);
	//// D → E
	world.addBefore(D, E);

	//// E → F
	world.addAfter(E, F);

	float dt = 0;

	//システム実行
	world.update(dt);
}