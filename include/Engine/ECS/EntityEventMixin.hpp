#ifndef ECS_ENTITYEVENTMIXIN_HPP
#define ECS_ENTITYEVENTMIXIN_HPP

#include "Engine/ECS/Events/CallBackListST.hpp"

namespace ECS{

    template<typename Derived>
    struct EntityEventMixin {
        // コールバックの登録
        auto& on_construct() { return construct; }
        auto& on_destroy() { return destroy; }

        // 利用側から呼び出す通知用メソッド
        void notify_construct(Entity::EntityID entity) { construct(entity); }
        void notify_destroy(Entity::EntityID entity) { destroy(entity); }

    private:
        using CallbackType = EVENT::CallbackList_Single<void(const Entity::EntityID)>;

    private:
        EVENT::CallbackList_Single<void(const Entity::EntityID)> construct;
        EVENT::CallbackList_Single<void(const Entity::EntityID)> destroy;
    };

}// namespace ECS

#endif // !ECS_ENTITYEVENTMIXIN_HPP

