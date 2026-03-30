#ifndef ECS_ENTITYEVENTMIXIN_HPP
#define ECS_ENTITYEVENTMIXIN_HPP

#include "CallbackList.hpp"

namespace ECS{

    template<typename Derived>
    struct EntityEventMixin {
        // コールバックの登録
        auto& on_construct() { return construct; }
        auto& on_destroy() { return destroy; }

        // 利用側から呼び出す通知用メソッド
        void notify_construct(EntityID entity) { construct(entity); }
        void notify_destroy(EntityID entity) { destroy(entity); }

    private:
        using CallbackType = EVENT::CallbackList<void(const EntityID)>;

    private:
        EVENT::CallbackList<void(const EntityID)> construct;
        EVENT::CallbackList<void(const EntityID)> destroy;
    };

}// namespace ECS

#endif // !ECS_ENTITYEVENTMIXIN_HPP

