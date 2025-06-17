#ifndef ECS_STORAGE_HPP
#define ECS_STORAGE_HPP

#include "Entity.h"
#include "SparseSet.h"
#include "CallbackList.hpp"
#include <type_traits>

namespace ECS{

// StorageType
enum class StorageType {
    BasicType,
    EventType,
    StorageTypeNum
};

// CRTP による共通インタフェース
template<typename Derived>
class StorageCRTP {
public:
    static constexpr StorageType get_storage_type() {
        return Derived::storage_t;
    }
};

// Event 向けミックスイン
class EventsMixin {
public:
    using CallbackType = EVENT::CallbackList<void(const EntityID)>;

    // コールバックの登録
    auto& on_construct() { return construct; }
    auto& on_update() { return update; }
    auto& on_destroy() { return destroy; } 

    // 利用側（派生クラス）から呼び出す通知用メソッド
    void notify_construct(EntityID entity) { construct(entity); }
    void notify_update(EntityID entity) { update(entity); }
    void notify_destroy(EntityID entity) { destroy(entity); }

private:
    EVENT::CallbackList<void(const EntityID)> construct;
    EVENT::CallbackList<void(const EntityID)> update;
    EVENT::CallbackList<void(const EntityID)> destroy;
};

// 基本ストレージ（コンポーネント管理のための SparseSet<T> をベースとする）
template<typename Type,StorageType ST = StorageType::BasicType>
class BasicStorage : public SparseSet<Type>,public StorageCRTP<BasicStorage<Type,ST>> {
public:
    using BaseType = ISparseSet;
    using type = Type;
    static constexpr StorageType storage_t = ST;
};

// Mixin を合成可能なストレージテンプレート
template<typename Type, StorageType S,typename... Mixins>
class MixinStorage : public BasicStorage<Type,S>,public Mixins... {
public:
    
};

// ストレージの型を選択するためのテンプレート特殊化
template<typename Type, StorageType S = StorageType::BasicType>
struct StorageClass {
    using type = BasicStorage<Type,StorageType::BasicType>;
};

// イベント用には EventsMixin を合成
template<typename Type>
struct StorageClass<Type, StorageType::EventType> {
    using type = MixinStorage<Type,StorageType::EventType,EventsMixin>;
};

template<typename Type, StorageType S = StorageType::EventType>
using StorageClass_t = typename StorageClass<Type, S>::type;


// ユーザーが使いやすいエイリアス

}//namespace ECS

#endif
