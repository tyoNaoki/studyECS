#pragma once
#include "TestFramework.hpp"
#include "Engine\ECS\World.h"

int test()
{
	RUN_TEST("test_SystemSchedule", 1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

void TestSystemB(ECS::World& world) {
	std::printf("B is updated\n");
}
void TestSystemC(ECS::World& world) {
	std::printf("C is updated\n");
}
void TestSystemD(ECS::World& world) {
	std::printf("D is updated\n");
}
void TestSystemE(ECS::World& world) {
	std::printf("E is updated\n");
}
void TestSystemF(ECS::World& world) {
	std::printf("F is updated\n");
}

namespace ECS::System {
	struct PreInitialization {};
	struct Initialization {};
}

TEST_CASE_PRIORITY(test_SystemSchedule) {
	ECS::World world;

	world = ECS::World();

	world.initialize();

	auto A = world.addSystem<ECS::System::PreInitialization>([](ECS::World& w) {
		std::printf("A is updated\n");
		});
	auto C = world.addSystem<ECS::System::PreInitialization>(TestSystemC);
	auto B = world.addSystem<ECS::System::PreInitialization>(TestSystemB);

	auto F = world.addSystem<ECS::System::Initialization>(TestSystemF);
	auto D = world.addSystem<ECS::System::Initialization>(TestSystemD);
	auto E = world.addSystem<ECS::System::Initialization>(TestSystemE);


	// A → B
	world.addBefore(A, B);

	// B → C
	world.addAfter(B, C);

	// D → E
	world.addBefore(D, E);

	// E → F
	world.addAfter(E, F);

	float dt = 0;
	//システム実行
	world.update(dt);
}