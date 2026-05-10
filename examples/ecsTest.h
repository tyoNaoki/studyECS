#pragma once

#include <tuple>
#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <cassert>
#include <random>

#include "Engine\Core\JobManager.h"
#include "Engine\ECS\World.h"
#include "TestFramework.hpp"

using namespace ECS::test;

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

void hashMapBenchmarks();

int ecs_Test()
{
	RUN_TEST("entityJobTest",1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

// 呼び出しフラグを立てるだけのダミーソート
struct DummySort {
	bool& flag;
	DummySort(bool& f) : flag(f) { ; }

	template<typename It, typename Compare, typename... Args>
	void operator()(It first, It last, Compare compare = Compare{}, Args &&...args) const {
		flag = true;
		std::sort(std::forward<Args>(args)..., std::move(first), std::move(last), std::move(compare));
	}
};

//カスタムソートアルゴリズムを渡す
TEST_CASE_PRIORITY(test_sort_with_custom_algo) {
	bool called = false;

	std::vector<int> v{ 2,1,3 };
	ECS::algorithm::sort(v, std::less<>{}, DummySort{ called });
	ASSERT(called, "custom sort algorithm called");
	ASSERT(std::is_sorted(v.begin(), v.end(),std::less<>{}), "custom algo sorts");
}

TEST_CASE(typeListTest)
{
	// テスト
	using MyList = ECS::type_list<int, double, char,float>;
	static_assert(ECS::type_Index_v<int, MyList> == 0, "int should be at index 0");
	static_assert(ECS::type_Index_v<double, MyList> == 1, "double should be at index 1");
	static_assert(ECS::type_Index_v<char, MyList> == 2, "char should be at index 2");
	static_assert(ECS::type_Index_v<float, MyList> != static_cast<std::size_t>(ECS::npos), "float is not in the list");
}

struct testBasicStorageComponent {
	static constexpr ECS::COMPONENT::StorageType storage_pref = ECS::COMPONENT::StorageType::BasicType;
};



TEST_CASE(entityTest)
{
	ECS::World world = ECS::World();

	world.initialize();

	Position position = Position(0.0f, 5.0f);
	auto entity = world.spawn("", position, Velocity(5.0f, 0.1f));
	auto entity2 = world.spawnEmpty();

	//auto entity2 = ECS::sWorld.spawnEmpty();
	world.emplaceOrUpdateComponent<Velocity>(entity2, 1.0f, 0.5f);
	world.emplaceOrUpdateComponent<Position>(entity2);
	//auto comp = ECS::world().getComponent<Position>(entity);

	//auto view2 = view.Exclude<Position>();
	//auto view = ECS::world().View<Velocity>(exclude_t<Position>{});

	/*
	for (size_t i = 0; i < packed.size(); i++) {
		Position a;
		std::tie(a) = packed[i].components;
	}
	*/
	auto view = world.View<Position, Velocity>();
	//auto view2 = view.Exclude<Position>();

	for (auto& x : view)
	{
		auto& entityID = x.entity;
		auto& vel = view.get<Velocity>(x.components);
		auto& posi = view.get<Position>(x.components);
		bool hasComp = world.has<Velocity>(entityID);
	}

	/*
	for (EntityID x : *view2)
	{
		bool hasComp = ECS::world().has<Velocity>(x);

	}
	*/

	for (auto [entityID, position, velocity] : view.each()) {
		auto id = entityID;
		auto posi = position;
		auto vel = velocity;
	}

	view.each([](auto entity, auto& pos, auto& vel) {
		pos.x += 5.0f;
		vel.x += 5.0f;
		});

	view.each([](auto& pos, auto& vel) {
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
	MESSAGE("view test Clear");
}

struct Cube {};
struct Transform {
	Transform() : rotation(3, 0.0f) {};

	std::vector<float>rotation;
};

struct cubeParallelJob : ECS::JobSystem::IParallelJob<cubeParallelJob>
{
	std::vector<Transform*> transforms;

	cubeParallelJob() {}
	
	inline void Execute(size_t index) {
		//回転
		transforms[index]->rotation[1] *= 0.1f;
	}
};

namespace ECS::System{
	struct Initialization{};
	struct Update{};
}

struct StartSpawnCubeEvent{};
struct StartTag {}; // Start 済みを示す

//キューブ(仮)を100体スポーンさせるイベント
class StartSystem : public ECS::System::SystemBase {
public:
	void onUpdate(ECS::World& world) override {
		// StartTag が付いていないエンティティだけ処理
		auto view = world.View<StartSpawnCubeEvent>(ECS::exclude_t<StartTag>{});

		for (auto [entityID, cubeEvent] : view.each()) {
			for (int i = 0; i < 100; i++) {
				world.spawn<Cube, Transform>();
			}
			
			//startTagをつける
			world.emplace<StartTag>(entityID);
		}

		/*view.each([&](auto entity, auto& startTag) {
			for (int i = 0; i < 100; i++) {
				world.spawn<Cube, Transform>();
			}

			world.emplace<StartTag>(entity);
			});*/

			/*for (auto& x : view)
			{
				for (int i = 0; i < 100; i++) {
					world.spawn<Cube,Transform>();
				}

				auto& entityID = x.entity;
				world.emplace<StartTag>(entityID);
			}*/
	}
};

//キューブ回転テスト
class RotationSystem : public ECS::System::SystemBase{
	std::shared_ptr<cubeParallelJob> job;
	
public:
	void onCreate(ECS::World & world)override {
		job = cubeParallelJob::create();
	}

	void onUpdate(ECS::World & world)override {
		// StartTag が付いていないエンティティだけ処理
		auto view = world.View<Cube, Transform>();
		job->transforms.clear();
		job->transforms.reserve(view.size());

		for (auto [entityID, cube, transform] : view.each()) {
			job->transforms.push_back(&transform);
		}

		auto handle = job->schedule(job->transforms.size(), 10, 7);
		handle.Complete();
	}
};

TEST_CASE(entityJobTest)
{
	//ジョブマネージャー初期化
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(1000, 7);

	//ワールド初期化
	ECS::World world = ECS::World();
	world.initialize();

	//StartSpawnCubeEventでキューブをスポーンさせる
	auto entity = world.spawn<StartSpawnCubeEvent>();

	//StartSystemを登録
	world.registerSystem<StartSystem,ECS::System::Initialization>();

	//RotationSystemはStartSystemの後に実行されるようにする
	world.registerSystem<RotationSystem,ECS::System::Update>();

	//ジョブマネージャー開始
	jm.start();

	//毎フレーム(仮)
	float dt = 0;
	for (int i = 0; i < 100; i++) {
		world.update(dt);
	}

	//Startシステム
	/*
	for (int i = 0; i < 100; i++) {
		auto entity = world.spawn<StartSpawnCubeEvent>();
	}

	
	auto parallelJob = cubeParallelJob::create();

	//Updateシステム
	
	for (int i = 0; i < 100; i++) {
		auto view = world.View<Transform, Cube>();
		parallelJob->transforms.clear();
		parallelJob->transforms.reserve(view.size());

		for (auto [entityID, transform, cube] : view.each()) {
			parallelJob->transforms.push_back(&transform);
		}

		auto handle = parallelJob->schedule(parallelJob->transforms.size(), 10, 7);
		handle.Complete();
	}
	*/

	MESSAGE("entityJobTest Clear");
}

//作成テスト
TEST_CASE(test_create_group) {
	ECS::World world = ECS::World();

	//システム初期化
	world.initialize();

	struct A {int x = 0;};
	struct B {int x = 10;};
	struct C {int x = 20;};
	struct D {int x = 30;};
	struct tagA{};

	bool isPrintSpawnLog = false;
	bool isPrintDespawnLog = false;

	//コンストラクターイベント追加
	auto spawnLog = [&](ECS::Entity::EntityID entt) {
		const auto index = entt.index;

		// ログ
		std::cout << "[Spawn] Entity " << index << " created\n";

		// デバッグ可視化（エディタ）
		//debugHierarchy.addEntity(index);

		// Start 的なもの
		if (world.has<A>(entt)) {
			auto* a = world.getComponent<A>(entt);
			std::cout << "[A] Entity " << index << " initialized\n";
		}

		if (world.has<B>(entt)) {
			auto* b = world.getComponent<B>(entt);
			std::cout << "[B] Entity " << index << " initialized\n";
		}

		// フラグ
		isPrintSpawnLog = true;
	};

	//デストラクターイベント追加
	auto destroyLog = [&](ECS::Entity::EntityID entt) {
		const auto index = ECS::Entity::GetEntityIndex(entt);

		// ログ
		std::cout << "[Destroy] Entity " << index << " removed\n";

		// エディタから削除
		//debugHierarchy.removeEntity(index);

		// 例：パーティクル再生
		if (world.has<A>(entt)) {
			auto* a = world.getComponent<A>(entt);
			std::cout << "[A] Entity " << index << " destroy\n";
			//fx.spawnAt(world.get<Transform>(entt).position);
		}

		// 子エンティティの破棄
		if (world.has<B>(entt)) {
			auto* b = world.getComponent<B>(entt);
			std::cout << "[B] Entity " << index << " destroy\n";
			/*for (auto child : world.get<Children>(entt).list) {
				world.destroy(child);
			}*/
		}
		
		std::cout << "World destory " << ECS::Entity::GetEntityIndex(entt) << " entity" << std::endl;
		isPrintDespawnLog = true;
	};

	//entiyスポーン時にイベント追加(ログ)
	world.entityPoolConstructor().append(spawnLog);
	world.entityPoolDestructor().append(destroyLog);

	auto emptyEntity = world.spawnEmpty();
	world.despawn(emptyEntity);

	auto entity = world.spawn<A,C,D>();
	auto entity1 = world.spawn<A, B,tagA>(A{ 1 });
	auto entity2 = world.spawn<A, B, tagA>("Apple", A{ 2 });
	auto entity3 = world.spawn<A, C>();
	auto entity4 = world.spawn<A, B, C, D>("Banana", A{ 4 });
	world.despawn(entity3);

	assertTrue(isPrintSpawnLog, "entityPoolConstructor() print Log on Spawn Entity");
	assertTrue(isPrintDespawnLog, "entityPoolDestructor() print Log on Despawn Entity");
	
	//assertEquals(ECS::world().getComponent<B>(entity)!=nullptr, "ECS::world().getComponent<B>(entity)!=nullptr");
	//assertEquals(ECS::world().getComponent<A>(entity4) != nullptr, "ECS::world().getComponent<A>(entity4)!=nullptr");

	//assertEquals(ECS::COMPONENT::component_storage_selector<A>::value == ECS::StorageType::EventType,"ECS::COMPONENT::component_storage_selector<A>::value == ECS::StorageType::EventType");

	//assertEquals(ECS::COMPONENT::component_storage_selector<testBasicStorageComponent>::value == ECS::StorageType::BasicType, "ECS::COMPONENT::component_storage_selector<testBasicStorageComponent>::value == ECS::StorageType::BasicType");

	//componentPool.
	
	auto& aPool = world.getComponentPool<C>();
	assertTrue(aPool.hasData(), "A struct is dataPool");
	world.emplaceOrUpdateComponent<tagA>(entity);
	auto& tagAPool = world.getComponentPool<tagA>();
	assertTrue(!tagAPool.hasData(),"tagA struct is emptyPool");

	//patch関数実行時に起きるように追加
	auto testOnUpdateFunction = ([](const ECS::Entity::EntityID& entity) {
		std::cout << ECS::Entity::GetEntityIndex(entity) << " has Updated" << std::endl;
	});
	aPool.on_update().append(testOnUpdateFunction);
	
	//第一引数のentityにその場で第二引数の関数実行
	aPool.patch(entity,[&entity](auto& pos) {
		std::cout << GetEntityIndex(entity) << " only patch Updating" << std::endl;
		});

	//poolに含まれる全てのEntityにその場で引数の関数実行する
	aPool.patch([](auto entity,auto& pos) {
		pos.x += 7;
		std::cout << GetEntityIndex(entity)<<" patch Updating"<< std::endl;
	});

	//auto group = ECS::world().group<A>(ECS::get<B>,ECS::exclude<tagA>);

	//std::cout<<"group.each(auto&compA,auto&compB)"<<std::endl;
	//group.each([](auto& compA,auto&compB) {
	//	std::cout << typeid(compB).name() << std::endl;
	//	//std::cout <<"compA is " << compA.x << std::endl;
	//	std::cout <<"compB is " << compB.x<< std::endl;
	//	});
	//std::cout << "group.each(const auto&entity,auto&compA,auto&compB)" << std::endl;

	//group.each([](const auto& entity, auto& compA,auto& compB){
	//	std::cout << GetEntityIndex(entity) << std::endl;
	//	//std::cout << "compA is " << compA.x << std::endl;
	//	std::cout << "compB is " << compB.x << std::endl;
	//	});

	//auto tupleAB = group.get<A,B>(entity4);
	//auto [tA, tB] = tupleAB;

	//std::cout << "auto [entt,a,b] : group.each" << std::endl;
	//for (auto [entt, a,b] : group.each()) {
	//	std::cout << GetEntityIndex(entt) << std::endl;
	//	std::cout << a.x << std::endl;
	//	std::cout << b.x << std::endl;
	//}

	//std::cout << "auto&x:group" << std::endl;
	//for(auto&x:group){
	//	std::cout<< GetEntityIndex(x) << std::endl;
	//}

	//assertTrue(group.contains(entity4),"group.contains(entity5) is true");
	//assertTrue(!group.contains(entity3),"group.contains(entity4) is false");

	//auto group2 = ECS::world().group<>(ECS::get<A,B,tagA>);
	////使用時、tupleではtagAは除外される

	//std::cout<<"group2.each(auto&a,auto&b)"<<std::endl;
	//group2.each([](auto& a,auto&b) {
	//	std::cout << a.x << std::endl;
	//	std::cout << b.x << std::endl;
	//	});
	//
	//std::cout << "group2.each(const auto&entity,auto&a,auto&b)" << std::endl;
	//group2.each([](const auto& entity,auto&a,auto&b) {
	//	std::cout << GetEntityIndex(entity) << std::endl;
	//	std::cout << a.x << std::endl;
	//	std::cout << b.x << std::endl;
	//	});

	//auto tuple2AB = group2.get<A,B,tagA>(entity2);
	//auto [t2A,t2B] = tuple2AB;
	//
	//std::cout << "auto [entt,a,b] : group2.each" << std::endl;
	//for(auto [entt,a,b] : group2.each()){
	//	std::cout << GetEntityIndex(entt) << std::endl;
	//	std::cout << a.x << std::endl;
	//	std::cout << b.x << std::endl;
	//}

	//std::cout << "auto&x:group2" << std::endl;
	//for (auto& x : group2) {
	//	std::cout << GetEntityIndex(x) << std::endl;
	//}
	//
	//assertTrue(group2.contains(entity1), "group2.contains(entity1) is true");
	//assertTrue(!group2.contains(entity3), "group2.contains(entity3) is false");

	auto sortGroup = world.group<A>(ECS::get<B>);
	auto poolA = sortGroup.getComponentPool<A>();

	auto& entity1afterRef = poolA->GetRef(entity1);
	//ASSERT(entity1afterRef.x == ECS::Entity::GetEntityIndex(entity1), "entity1 GetRef faild");

	std::cout<<"sort\n";

	for(auto&x:sortGroup){
		std::cout<<ECS::Entity::GetEntityIndex(x)<<",";
	}

	std::cout << "\n";

	sortGroup.sort([](const ECS::Entity::EntityID& a, const ECS::Entity::EntityID& b) {
		return a.index < b.index;
		});

	for (auto& x : sortGroup) {
		std::cout << ECS::Entity::GetEntityIndex(x) << ",";
	}

	std::cout<<std::endl;

	auto& entity1Ref =  poolA->GetRef(entity1);
	std::cout<<ECS::Entity::GetEntityIndex(entity1)<<"\n";
	ASSERT(entity1Ref.x == ECS::Entity::GetEntityIndex(entity1),"entity1 GetRef faild");
	auto& entity2Ref = poolA->GetRef(entity2);
	ASSERT(entity2Ref.x == ECS::Entity::GetEntityIndex(entity2), "entity2 GetRef faild");
	auto& entity4Ref = poolA->GetRef(entity4);
	ASSERT(entity4Ref.x == ECS::Entity::GetEntityIndex(entity4), "entity4 GetRef faild");

	for (auto& x : poolA->GetEntityList()) {
		std::cout << ECS::Entity::GetEntityIndex(x) << ",";
	}

	std::cout<<"\n";

	for(auto &x:poolA->GetValues()){
		std::cout<<x.x << ",";
	}

	//auto group = ECS::world().group<ECS::StorageType::EventType>(ECS::get<A,B>);
	//auto group2 = ECS::world().group<ECS::StorageType::EventType,B>();
	//auto group3 = ECS::world().group<ECS::StorageType::EventType,C>();
	/*
	auto componentPoolA = group.getComponentPool<A>();

	int index = 0;
	for(auto &x:componentPoolA->GetEntityList()){
		std::cout<<index<<" : " << GetEntityIndex(x)-1 << std::endl;
		index++;
	}
	*/

	/*
	auto& componentPool2 = ECS::world().getComponentPool<B>();
	index = 0;
	for (auto& x : componentPool2.GetEntityList()) {
		std::cout
		<< "[" << index << "] " << "B Valid Entity is " << GetEntityIndex(x) << std::endl;
		index++;
	}
	*/

	//ECS::world().removeComponent<A>(entity3);

	/*
	auto entity = ECS::world().spawn<A, B>(A{}, B{});
	auto entity2 = ECS::world().spawn<A, B>();
	auto entity3 = ECS::world().spawn<A, B>("Apple");
	auto entity4 = ECS::world().spawn<A, B>(B{});
	auto entity5 = ECS::world().spawn<A, B>("Banana", B{});
	*/
	//assertEquals(group.node<A>()->Size() != 0 , "Owner::BaseType does not have 'type'!");
	
	/*
	assertEquals(group.count() == 3, "ECS::world().getGroupSize()==3");  // 初期状態ではエンティティなし
	
	assertEquals(group.node<A>() != nullptr, "group.node<A>() != nullptr");
	assertEquals(group.node<C>() != nullptr, "group.node<C>() != nullptr");
	assertEquals(group.node<D>() == nullptr, "group.node<D>() == nullptr");
	std::cout << "node<B>.typename : " <<typeid(group.node<B>()).name() << std::endl;
	assertEquals(group.node<A>()->Size() == 0, "group.node<A>()->Size() == 0");
	ECS::world().spawn("",A{});
	ECS::world().spawn("",A{});
	ECS::world().spawn("",B{});
	assertEquals(group.node<A>()->Size() != 0, "Spawn A valid Entity. group.node<A>()->Size() != 0");
	
	*/
	//assertEquals(group.entityCount<A,B>()[1] == 1, "group.storageSize()[1] == 1");
	
	//group.checkTypeList<owned_t<A>();
}

/*
//追加テスト
void test_add_entity_to_group() {
	Group group;
	Entity entity = create_entity();
	group.add(entity);

	assert(group.contains(entity));  // グループに追加されているかチェック！
}
//除外対象のコンポーネントの適用
void test_exclude_component() {
	Group group;
	Entity entity = create_entity();
	group.add(entity);
	group.exclude<ComponentX>();

	assert(!group.contains(entity));  // 除外設定後に削除されるか確認！
}

//イベント適用
void test_event_trigger() {
	Group group;
	Entity entity = create_entity();

	bool constructed = false, destroyed = false;

	group.on_construct().connect([&](Entity e) { if (e == entity) constructed = true; });
	group.on_destroy().connect([&](Entity e) { if (e == entity) destroyed = true; });

	group.add(entity);
	assert(constructed);  // 構築イベントが発火したかチェック！

	group.remove(entity);
	assert(destroyed);  // 破棄イベントが発火したかチェック！
}
*/

TEST_CASE(test_sort_empty) {
	std::vector<int> v;
	ECS::algorithm::sort(v, std::less<>{});
	assertTrue(v.empty(), "empty vector remains empty");
}

// 要素1個
TEST_CASE(test_sort_single) {
	std::vector<int> v{ 42 };
	ECS::algorithm::sort(v, std::less<>{});
	assertTrue(v.size() == 1 && v[0] == 42, "single element stays unchanged");
}

// 昇順ソート
TEST_CASE(test_sort_ascending) {
	std::vector<int> v{ 4,3,2,1,0 };
	ECS::algorithm::sort(v, std::less<>{});
	for (auto& x : v) {
		std::cout << x << ",";
	}
	std::cout << "\n";
	assertTrue(std::is_sorted(v.begin(), v.end()),
		"reverse-based sort yields descending");
}

// 降順ソート (std::greater)
TEST_CASE(test_sort_descending) {
	std::vector<int> v{ 3,1,4,2,5 };
	ECS::algorithm::sort(v, std::greater<>{});
	assertTrue(std::is_sorted(v.begin(), v.end(), std::greater<>{}),
		"reverse-based sort yields descending");
}

// 既にソート済み 
TEST_CASE(test_sort_already_sorted) {
	std::vector<int> v{ 1,2,3,4,5 };
	ECS::algorithm::sort(v, std::greater<>{});
	assertTrue(std::is_sorted(v.begin(), v.end(), std::greater<>{}), "already sorted remains sorted");
}

// テスト関数
TEST_CASE_DISABLED(testGroupIdentifiers) {
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	//
	//using GroupA = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity>, exclude_t<>>;
	//using GroupB = ECS::Group<owned_t<OwnerB>, get_t<Position, Velocity>, exclude_t<>>;
	//using GroupC = ECS::Group<owned_t<OwnerC>, get_t<Position, Velocity>, exclude_t<>>;
	//using GroupD = ECS::Group<owned_t<OwnerA>, get_t<Position>, exclude_t<>>;
	//using GroupE = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity, Health>, exclude_t<>>;

	//// 識別 ID を取得
	//ecs_map::id_type id_A = GroupA::group_id();
	//ecs_map::id_type id_B = GroupB::group_id();
	//ecs_map::id_type id_C = GroupC::group_id();
	//ecs_map::id_type id_D = GroupD::group_id();
	//ecs_map::id_type id_E = GroupE::group_id();

	//// 結果を出力
	//std::cout << "GroupA ID: " << id_A << std::endl;
	//std::cout << "GroupB ID: " << id_B << std::endl;
	//std::cout << "GroupC ID: " << id_C << std::endl;
	//std::cout << "GroupD ID: " << id_D << std::endl;
	//std::cout << "GroupE ID: " << id_E << std::endl;

	//// 識別 ID の整合性チェック
	//assert(id_A != id_B && "OwnerA と OwnerB のグループ ID が同じ");
	//assert(id_A != id_C && "OwnerA と OwnerC のグループ ID が同じ");
	//assert(id_A != id_D && "異なるコンポーネントセットなのに同じグループ ID");
	//assert(id_A != id_E && "Health コンポーネントがあるのに ID が変わらない");

	//std::cout << "すべての識別 ID のテストが正常に完了しました！" << std::endl;
}


bool executeTimeTest()
{
	auto world = ECS::World();
	world.initialize();

	auto start_creation = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < 1000000; i++)
	{
		auto entity = world.spawn();
		world.emplaceOrUpdateComponent<TransformComponent>(entity, 1.0f, 2.0f, 3.0f);
	}

	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "エンティティの作成とコンポーネントの追加: " << duration_creation.count() << " ミリ秒\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto [entity, transform] : world.View<TransformComponent>().each()) {
		transform._x += 1.0f;
		transform._y += 1.0f;
		transform._z += 1.0f;
	}

	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "コンポーネントの変更にかかった時間: " << duration_modification.count() << " ミリ秒\n";

	return true;
}

///////////////////////// 通常のコンポーネント指向////////////////////////////
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
	// gameObject(1000000)とするのはフェアじゃなさそうなので1つずつpush_backしてます。
	for (size_t i = 0; i < 1000000; i++)
	{
		GameObject gameObject;
		gameObject.addComponent<NormalTransformComponent>(1.0f, 2.0f, 3.0f);
		gameObjects.push_back(gameObject);
	}
	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "GameObjectの作成とコンポーネントの追加: " << duration_creation.count() << " ミリ秒\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto& gameObject : gameObjects) {
		gameObject.update();
	}
	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "コンポーネントの変更にかかった時間: " << duration_modification.count() << " ミリ秒\n";

	return true;
}
/// /////////////////////////////////////////////

//hash関係計測





