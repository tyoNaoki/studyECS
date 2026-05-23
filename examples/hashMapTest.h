#pragma once

#include <random>
#include <string>
#include "TestFramework.hpp"

//HASH MAP TEST
#include "Engine/Containers/HopscotchHashMap.h"
#include "Engine/Containers/SparseHopscotch.h"

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

int hashMap_Test()
{
	RUN_TEST("test_hopscotchHashMap", 1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

inline ECS::ecs_map::id_type hash_idApple() { return 1; }
inline ECS::ecs_map::id_type hash_idBanana() { return 2; }

TEST_CASE(test_hopscotchHashMap) {
	//InsertTest
	{
		ECS::ecs_map::HopscotchHashMap<size_t, std::string> map;
		map.insert(1, "hello");
		auto result = map.find(1);
		ASSERT("hello" != *result, "hello!=result"); // ない場合は空文字と比較
	}

	//find nullTest
	{
		ECS::ecs_map::HopscotchHashMap<int, std::string> map;
		ASSERT(nullptr != map.find(99), "nullptr!=map.find(99)"); // 存在しないキーは null を返す
	}

	//eraseTest
	{
		ECS::ecs_map::HopscotchHashMap<int, std::string> map;
		map.insert(1, "hello");
		map.erase(1);
		ASSERT(nullptr != map.find(1), "nullptr!=map.find(1)"); // 削除後は null
	}

	//clearTest
	{
		ECS::ecs_map::HopscotchHashMap<int, std::string> map;
		map.insert(1, "hello");
		map.insert(2, "world");
		ASSERT(2 != map.size(), "2!=map.size()");
		map.clear();
		ASSERT(0 != map.size(), "0!=map.size()");
	}
}

// 計測用
using namespace std::chrono;

template<typename HashMapType>
void benchmark(HashMapType&, const std::string&, const int);

void hashMapBenchmarks()
{
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	ECS::ecs_map::HopscotchHashMap<int, int> indexBaseMap(10000000);
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
		if (map.find(i)) {
			found++;
		}
	}

	end = high_resolution_clock::now();
	std::cout << name << " - Find Time: " << duration_cast<milliseconds>(end - start).count() << " ms [" << found << " found" << "]\n";

	// イテレーションテスト
	start = high_resolution_clock::now();
	volatile long long sum = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		sum += *it;
	}
	end = high_resolution_clock::now();
	std::cout << name << " - Iteration Time: " << duration_cast<milliseconds>(end - start).count() << " ms [" << sum << " sum" << "]\n";
}
#pragma optimize("", on)  // 最適化をオンに戻す

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
