#pragma once
#include "Debug.h"
#include "Scene.h"
#include "Component.h"
#include "World.h"
#include "SceneView.h"
#include <tuple>
#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include "group.h"
#include <cassert>

struct Position
{
	Position(float _x, float _y) :x(_x), y(_y) {};
	Position() = default;
	float x = 0;
	float y = 0;
};

struct  Velocity
{
	Velocity(float _x, float _y) :x(_x), y(_y) {};
	Velocity() = default;
	float x = 0;
	float y = 0;
};

struct TransformComponent {
	float _x, _y, _z;

	TransformComponent(float x = 0, float y = 0, float z = 0) : _x(x), _y(y), _z(z) {}
};

void test()
{
	
}

// �e�X�g�֐�
void testGroupIdentifiers() {
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	using GroupA = basic_group<owned_t<OwnerA>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupB = basic_group<owned_t<OwnerB>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupC = basic_group<owned_t<OwnerC>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupD = basic_group<owned_t<OwnerA>, get_t<Position>, exclude_t<>>;
	using GroupE = basic_group<owned_t<OwnerA>, get_t<Position, Velocity, Health>, exclude_t<>>;

	// ���� ID ���擾
	id_type id_A = GroupA::group_id();
	id_type id_B = GroupB::group_id();
	id_type id_C = GroupC::group_id();
	id_type id_D = GroupD::group_id();
	id_type id_E = GroupE::group_id();

	// ���ʂ��o��
	std::cout << "GroupA ID: " << id_A << std::endl;
	std::cout << "GroupB ID: " << id_B << std::endl;
	std::cout << "GroupC ID: " << id_C << std::endl;
	std::cout << "GroupD ID: " << id_D << std::endl;
	std::cout << "GroupE ID: " << id_E << std::endl;

	// ���� ID �̐������`�F�b�N
	assert(id_A != id_B && "OwnerA �� OwnerB �̃O���[�v ID ������");
	assert(id_A != id_C && "OwnerA �� OwnerC �̃O���[�v ID ������");
	assert(id_A != id_D && "�قȂ�R���|�[�l���g�Z�b�g�Ȃ̂ɓ����O���[�v ID");
	assert(id_A != id_E && "Health �R���|�[�l���g������̂� ID ���ς��Ȃ�");

	std::cout << "���ׂĂ̎��� ID �̃e�X�g������Ɋ������܂����I" << std::endl;
}

void entityTest()
{
	Position position = Position(0.0f, 5.0f);
	auto entity = ECS::world().spawn("", position, Velocity(5.0f, 0.1f));
	auto entity2 = ECS::world().spawnEmpty();

	//auto entity2 = ECS::sWorld.spawnEmpty();
	ECS::world().emplace<Velocity>(entity2, 1.0f, 0.5f);
	ECS::world().emplace<Position>(entity2);
	//auto comp = ECS::world().getComponent<Position>(entity);

	//auto view2 = view.Exclude<Position>();
	//auto view = ECS::world().View<Velocity>(exclude_t<Position>{});

	/*
	for (size_t i = 0; i < packed.size(); i++) {
		Position a;
		std::tie(a) = packed[i].components;
	}
	*/
	auto view = ECS::world().View<Position, Velocity>();
	//auto view2 = view->Exclude<Position>();

	for (auto& x : *view)
	{
		auto& entityID = x.entity;
		auto& vel = view->get<Velocity>(x.components);
		auto& posi = view->get<Position>(x.components);
		//bool hasComp = ECS::world().has<Velocity>(view->get<EntityID>(x));
	}

	/*
	for (EntityID x : *view2)
	{
		bool hasComp = ECS::world().has<Velocity>(x);

	}
	*/

	for (auto [entityID, position, velocity] : view->each()) {
		auto id = entityID;
		auto posi = position;
		auto vel = velocity;
	}

	view->each([](auto entity, auto& pos, auto& vel) {
		pos.x += 5.0f;
		vel.x += 5.0f;
		});

	view->each([](auto& pos, auto& vel) {
		pos.x += 5.0f;
		vel.x += 5.0f;
		});

	//auto entities = scene.getWorld().findEntitiesWithComponents<Velocity>();
	//auto comp = scene.getWorld().getComponent<Position>(entity);
	//scene.getWorld().removeComponent<Position>(entity);

	//comp->x+= 10.0f;
	//comp = scene.getWorld().getComponent<Position>(entity);

	//scene.getWorld().despawn(entity);
	//scene.getWorld().despawn(entity);
}

bool executeTimeTest()
{
	auto& world = ECS::world();
	auto start_creation = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < 1000000; i++)
	{
		auto entity = world.spawn();
		world.emplace<TransformComponent>(entity, 1.0f, 2.0f, 3.0f);
	}

	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "�G���e�B�e�B�̍쐬�ƃR���|�[�l���g�̒ǉ�: " << duration_creation.count() << " �~���b\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto [entity, transform] : ECS::world().View<TransformComponent>()->each()) {
		transform._x += 1.0f;
		transform._y += 1.0f;
		transform._z += 1.0f;
	}

	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "�R���|�[�l���g�̕ύX�ɂ�����������: " << duration_modification.count() << " �~���b\n";

	return true;
}

///////////////////////// �ʏ�̃R���|�[�l���g�w��////////////////////////////
class NormalComponent {
public:
	virtual void update() = 0;
	virtual ~NormalComponent() {}
};

class NormalTransformComponent : public NormalComponent {
public:
	float _x, _y, _z;

	NormalTransformComponent(float x = 0, float y = 0, float z = 0) : _x(x), _y(y), _z(z) {}

	void update() override {
		_x += 1.0f;
		_y += 1.0f;
		_z += 1.0f;
	}
};

class GameObject {
private:
	std::vector<std::shared_ptr<NormalComponent>> _components;

public:
	GameObject() {}

	template<typename T, typename... Args>
	void addComponent(Args&&... args) {
		_components.push_back(std::make_shared<T>(std::forward<Args>(args)...));
	}

	void update() {
		for (auto& component : _components) {
			component->update();
		}
	}
};

bool executeTimeTest_NormalComponentBase() {
	auto start_creation = std::chrono::high_resolution_clock::now();

	std::vector<GameObject> gameObjects;
	// gameObject(1000000)�Ƃ���̂̓t�F�A����Ȃ������Ȃ̂�1����push_back���Ă܂��B
	for (size_t i = 0; i < 1000000; i++)
	{
		GameObject gameObject;
		gameObject.addComponent<NormalTransformComponent>(1.0f, 2.0f, 3.0f);
		gameObjects.push_back(gameObject);
	}
	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "GameObject�̍쐬�ƃR���|�[�l���g�̒ǉ�: " << duration_creation.count() << " �~���b\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto& gameObject : gameObjects) {
		gameObject.update();
	}
	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "�R���|�[�l���g�̕ύX�ɂ�����������: " << duration_modification.count() << " �~���b\n";

	return true;
}
/// /////////////////////////////////////////////