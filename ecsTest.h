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
#include <cassert>
#include "HopscotchHashMap.h"
#include "SparseHopscotch.h"
#include "unodreredDense.h"
#include <random>
#include "GroupNode.hpp"

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

void test_create_group();
void hashMapBenchmarks();
void typeListTest();
void eventTest();

void test()
{
	eventTest();
}

void eventTest()
{

}

void typeListTest()
{
	// テスト
	using MyList = ECS::type_list<int, double, char,float>;
	static_assert(ECS::type_Index_v<int, MyList> == 0, "int should be at index 0");
	static_assert(ECS::type_Index_v<double, MyList> == 1, "double should be at index 1");
	static_assert(ECS::type_Index_v<char, MyList> == 2, "char should be at index 2");
	static_assert(ECS::type_Index_v<float, MyList> != static_cast<std::size_t>(ECS::npos), "float is not in the list");
}

// --- テスト関数 ---
//<Key,Value>
//benchmark(IndexBase_HopscotchHashMap<int,int>(capacity : 8u), "IndexBase");
//benchmark(SparseSetBase_HopscotchHashMap<int, int> sparseSetMap(cap : 100000), "SparseSet");
/*
結果の分析(Debug)
##挿入時間 (IndexBase: 18ms → SparseSet: 15ms)
- SparseSetとほぼ同じレベルまで短縮され、バケット探索のオーバーヘッドが削減されたことがわかる。
- Hopscotchの局所性を活かしつつ、リハッシュの影響を抑えられた ことが成功の要因かも。
##検索時間 (IndexBase: 4ms / SparseSet: 5ms)
- ほぼ差がなく、どちらの方式も高速な検索ができている。
- これは適切なハッシュ計算とバケット配置のおかげ。
##イテレーション時間 (IndexBase: 4ms → SparseSet: 12ms)
- SparseSet方式は dense にデータを密集させるために余計なループ処理が発生しやすいかも。
- 一方、IndexBase方式は next() による空バケットスキップが効率的に機能している。

ankerl::unordered_dense::map - Insert Time: 71 ms
ankerl::unordered_dense::map - Find Time: 47 ms [100000 found]
ankerl::unordered_dense::map - Iteration Time: 11 ms [1409965408 sum]

結果の分析(Release)
##挿入時間 (Insert Time)
- IndexBase: 452ms
- SparseSet: 157ms
SparseSet方式のほうが約3倍高速にデータを挿入 できています。
これは、SparseSetが 連続したメモリアクセスを活用し、キャッシュミスを最小限に抑える ためです。
IndexBase方式では ホップ情報 (hopinfoes_) の管理やバケット移動 (moveEmpty()) が挿入のコストを増やしている 可能性があります。
##検索 (Find Time)
- IndexBase: 237ms
- SparseSet: 279ms
IndexBase方式のほうが若干検索が速い（SparseSetより約42ms短縮）。
これは、IndexBase方式が ホップ情報を活用して、局所性の高い検索ができるため です。
一方、SparseSetは 探索するテーブルが小さいので、検索自体は比較的軽いが、メモリアクセスがやや分散している可能性がある。
##イテレーション (Iteration Time)
- IndexBase: 9ms
- SparseSet: 2ms
SparseSet方式のほうが約4倍高速にイテレーションできている。
SparseSetは dense にデータを格納するため、シーケンシャルアクセスが最適化される のに対し、
IndexBase方式では バケットをスキップしながらイテレーションするため、オーバーヘッドが発生している可能性 があります。
*/

template<typename HashMapType>
void benchmark(HashMapType&, const std::string&, const int);

void hashMapBenchmarks()
{
	
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	ecs_map::HopscotchHashMap<int, int> indexBaseMap(10000000);
	//ecs_map::SparseHopscotchHashMap<int,int> sparseSetMap(10000);
	//ecs_map::unordered_dense_map<int,int> unordered_denseMap(10000);

	std::cout << "Benchmarking...\n";
	benchmark(indexBaseMap, "IndexBase", 10000000);

	//benchmark(sparseSetMap, "SparseSet", 10000);
}

#pragma optimize("", off)  // 最適化を無効化
template<typename HashMapType>
void benchmark(HashMapType& map, const std::string& name, const int numElements) {
	using namespace std::chrono;

	// 挿入テスト
	auto start = high_resolution_clock::now();
	for (int i = 0; i < numElements; ++i) {
		map.insert(i, i * 2);
	}
	auto end = high_resolution_clock::now();
	std::cout << name << " - Insert Time: " << duration_cast<milliseconds>(end - start).count() << " ms\n";	

	// 検索テスト
	start = high_resolution_clock::now();
	int found = 0;
	for (int i = 0; i < numElements; ++i) {
		if (map.find(i)){
			found++;
		}
	}

	end = high_resolution_clock::now();
	std::cout << name << " - Find Time: " << duration_cast<milliseconds>(end - start).count() << " ms ["<< found<<" found"<< "]\n";

	// イテレーションテスト
	start = high_resolution_clock::now();
	volatile long long sum = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		sum += *it;
	}
	end = high_resolution_clock::now();
	std::cout << name << " - Iteration Time: " << duration_cast<milliseconds>(end - start).count() << " ms [" << sum <<" sum" << "]\n";
}
#pragma optimize("", on)  // 最適化をオンに戻す

//作成テスト
inline void test_create_group() {
	struct A {};
	struct B {};
	struct C {};
	struct D {};
	assertEquals(ECS::world().CreateGroupNode<ECS::GroupType::basicGroup,A>()!=nullptr, "ECS::world().CreateGroupNode<GroupType::basicGroup,A>()!=nullptr");

	auto group = ECS::world().group<ECS::GroupType::basicGroup,A>(ECS::get<B>,ECS::exclude<C>);
	
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

// テスト関数
void testGroupIdentifiers() {
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	/*
	using GroupA = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupB = ECS::Group<owned_t<OwnerB>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupC = ECS::Group<owned_t<OwnerC>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupD = ECS::Group<owned_t<OwnerA>, get_t<Position>, exclude_t<>>;
	using GroupE = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity, Health>, exclude_t<>>;

	// 識別 ID を取得
	ecs_map::id_type id_A = GroupA::group_id();
	ecs_map::id_type id_B = GroupB::group_id();
	ecs_map::id_type id_C = GroupC::group_id();
	ecs_map::id_type id_D = GroupD::group_id();
	ecs_map::id_type id_E = GroupE::group_id();

	// 結果を出力
	std::cout << "GroupA ID: " << id_A << std::endl;
	std::cout << "GroupB ID: " << id_B << std::endl;
	std::cout << "GroupC ID: " << id_C << std::endl;
	std::cout << "GroupD ID: " << id_D << std::endl;
	std::cout << "GroupE ID: " << id_E << std::endl;

	// 識別 ID の整合性チェック
	assert(id_A != id_B && "OwnerA と OwnerB のグループ ID が同じ");
	assert(id_A != id_C && "OwnerA と OwnerC のグループ ID が同じ");
	assert(id_A != id_D && "異なるコンポーネントセットなのに同じグループ ID");
	assert(id_A != id_E && "Health コンポーネントがあるのに ID が変わらない");

	std::cout << "すべての識別 ID のテストが正常に完了しました！" << std::endl;
	*/
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
	std::cout << "エンティティの作成とコンポーネントの追加: " << duration_creation.count() << " ミリ秒\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto [entity, transform] : ECS::world().View<TransformComponent>()->each()) {
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

// 計測用
using namespace std::chrono;

// FNV-1a ハッシュ
constexpr uint64_t hash_FNV1a(std::string_view sv) {
	constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;
	constexpr uint64_t FNV_PRIME = 0x100000001b3;

	uint64_t hash = FNV_OFFSET_BASIS;
	for (char c : sv) {
		hash ^= static_cast<uint64_t>(c);
		hash *= FNV_PRIME;
	}
	return hash;
}

// ローリングハッシュ (基数: 31, MOD: 1e9+7)
constexpr uint64_t hash_Rolling(std::string_view sv) {
	constexpr uint64_t BASE = 31;
	constexpr uint64_t MOD = 1000000007;

	uint64_t hash = 0;
	uint64_t power = 1;

	for (char c : sv) {
		hash = (hash * BASE + static_cast<uint64_t>(c)) % MOD;
		power = (power * BASE) % MOD;
	}
	return hash;
}

std::string random_string(size_t length) {
	static std::mt19937_64 rng(std::random_device{}());
	static std::uniform_int_distribution<int> dist(97, 122);  // 'a' ~ 'z'

	std::string s;
	s.reserve(length);
	for (size_t i = 0; i < length; ++i) {
		s += static_cast<char>(dist(rng));  // キャストを追加
	}
	return s;
}

void hashTimetest() {
	constexpr size_t TEST_CASES = 100000;  // テスト回数
	constexpr size_t STRING_LENGTH = 50;   // 文字列長

	std::vector<std::string> test_strings;
	for (size_t i = 0; i < TEST_CASES; ++i) {
		test_strings.push_back(random_string(STRING_LENGTH));
	}

	// FNV-1a ハッシュの測定
	auto start_FNV1a = high_resolution_clock::now();
	for (const auto& str : test_strings) {
		volatile uint64_t hash = hash_FNV1a(str);
	}
	auto end_FNV1a = high_resolution_clock::now();
	auto duration_FNV1a = duration_cast<microseconds>(end_FNV1a - start_FNV1a).count();

	// ローリングハッシュの測定
	auto start_Rolling = high_resolution_clock::now();
	for (const auto& str : test_strings) {
		volatile uint64_t hash = hash_Rolling(str);
	}
	auto end_Rolling = high_resolution_clock::now();
	auto duration_Rolling = duration_cast<microseconds>(end_Rolling - start_Rolling).count();

	// 結果出力
	std::cout << "FNV-1a Hash Time: " << duration_FNV1a << " s\n";
	std::cout << "Rolling Hash Time: " << duration_Rolling << " s\n";

	//FNV - 1a: 17, 445
	//Rolling Hash : 62, 311
}



