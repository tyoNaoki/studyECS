#ifndef ECS_STORAGE_HPP
#define ECS_STORAGE_HPP

#include "Entity.h"
#include "SparseSet.h"

// 1. エンティティの型定義（基本は独自の EntityType を利用）
enum class StorageClass{
    basicStorage,
    eventStorage,
    customStorage
};

enum class Entity : EntityType{};

// 2. コンポーネントの基本ストレージ
//    Type は格納対象のコンポーネント型。
//    Entity はエンティティ識別子の型。デフォルトは前述の Entity。
template<typename Type,typename Entity =  EntityType>
class BasicStorage : public SparseSet<Entity> {
public:
    using BaseType = SparseSet<Entity>;
    using type = Type;
    // 拡張性を考慮して、格納する型もエイリアス化しておく
    // ここに共通処理を追加してゆく（例えばシリアライズ、デバッグ表示など）
};

// 3. Reactive（イベント対応の）ストレージ（将来拡張用）
//    現状は BasicStorage と同じですが、イベント発行や反応用の機能を後から追加できる余地を残す。
template<typename Type>
class EventStorage : public BasicStorage<Type,Entity> {
public:
    using BaseType = SparseSet<Entity>;
    // 将来的にイベント関連の機能――たとえばコールバック登録や通知メソッド――を実装できる
};

template<typename Type>
class CustomStorage : public BasicStorage<Type, Entity> {
public:
    using BaseType = SparseSet<Entity>;

    void info() { std::cout << "CustomStorage\n"; }
};

// 4. ストレージ型を決定するためのメタ関数
//    将来的に型ごとに別のストレージ（例: EventStorage）を選びたい場合、ここを特殊化すればよい
template<typename Type, StorageClass S = StorageClass::basicStorage>
struct StorageType {

    using type = BasicStorage<Type,Entity>;
};

template<typename Type>
struct StorageType<Type, StorageClass::eventStorage> {
    using type = EventStorage<Type>;
};

template<typename Type>
struct StorageType<Type, StorageClass::customStorage> {
    using type = CustomStorage<Type>;
};

#endif