#ifndef ECS_STORAGE_HPP
#define ECS_STORAGE_HPP

#include "Entity.h"
#include "SparseSet.h"
#include <type_traits>
#include "StorageMixin.hpp"

namespace ECS{

namespace COMPONENT{

// StorageType
enum class StorageType {
    BasicType,
    EventType,
    StorageTypeNum
};

// CRTP による共通インタフェース
template<typename Derived>
struct StorageCRTP {
    static constexpr StorageType get_storage_type() {
        return Derived::storage_t;
    }
};

// 基本ストレージ（コンポーネント管理のための SparseSet<T> をベースとする）
template<typename Type,StorageType ST = StorageType::BasicType>
class BasicStorage : public SparseSet<Type>,public StorageCRTP<BasicStorage<Type,ST>> {
public:
    using BaseType = ISparseSet;
    using UnderingType = SparseSet<Type>;
    using value_type = Type;
    static constexpr StorageType storage_t = ST;
    static constexpr bool has_data = !std::is_empty_v<value_type>;

    // テンプレートメソッドなので virtual にできない点に注意
    template<typename... Args>
    Type* Emplace(EntityID entity, Args&&... args) {
        // ここではなんの前後処理もせず、親 (SparseSet<T>) の emplace を呼ぶだけ
        return UnderingType::Emplace(entity, std::forward<Args>(args)...);
    }

    template<typename... Args>
    Type* Update(EntityID entity, Args&&... args) {
        // ここではなんの前後処理もせず、親 (SparseSet<T>) の emplace を呼ぶだけ
        return UnderingType::Update(entity, std::forward<Args>(args)...);
    }

    template<typename F>
    void patch(EntityID entityID, F&& fn) {
        assert(false);
    }
};

// Mixin を合成可能なストレージテンプレート
template<typename Type, StorageType S,typename... Mixins>
class MixinStorage : public BasicStorage<Type,S>,public Mixins... {
    // 基底の Emplace を参照できるように using 宣言
    using Base = BasicStorage<Type, S>;
public:
    using typename Base::value_type;

    template<typename... Args>
    value_type* Emplace(const EntityID& entityID, Args&&... args) {
        this->notify_construct(entityID);
        
        return Base::Emplace(entityID, std::forward<Args>(args)...);
    }

    template<typename... Args>
    value_type* Update(const EntityID& entityID, Args&&... args) {
        this->notify_update(entityID);

        return Base::Update(entityID, std::forward<Args>(args)...);
    }

    template<typename F>
    void patch(F&& fn) {
        using OneArg = std::is_invocable<F, Type&>;
        using TwoArg = std::is_invocable<F, EntityID, Type&>;

        static_assert(OneArg::value || TwoArg::value,
            "patch() に渡す fn は void(T&) または void(EntityID, T&) の形にしてください");

        for(auto it = Base::begin();it!=Base::end();it++){
            EntityID entityID = *it;
            auto& comp = this->GetRef(entityID);

            if constexpr (OneArg::value) {
                // １引数版
                std::forward<F>(fn)(comp);
            }
            else if constexpr (TwoArg::value) {
                // ２引数版
                std::forward<F>(fn)(entityID, comp);
            }

            this->notify_update(entityID);
        }
        return;
    }

    template<typename F>
    void patch(EntityID entityID, F&& fn) {
        assert(this->ContainsEntity(entityID));

        using OneArg = std::is_invocable<F, Type&>;
        static_assert(OneArg::value,
            "patch() に渡す fn は void(T&) の形にしてください");

        auto& comp = this->GetRef(entityID);

        if constexpr (OneArg::value) {
            std::forward<F>(fn)(comp);
        }

        //変更後に通知
        this->notify_update(entityID);
    }

    void Delete(const EntityID& entityID){
        this->notify_destroy(entityID);

        return Base::Delete(entityID);
    }
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

}//namespace COMPONENT_STORAGE

}//namespace ECS

#endif
