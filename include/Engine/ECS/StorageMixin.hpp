#ifndef ECS_STORAGEMIXIN_HPP
#define ECS_STORAGEMIXIN_HPP
#include "Engine\ECS\Events\CallBackListST.hpp"
#include "Entity.h"

namespace ECS{

namespace COMPONENT{

    // Event 向けミックスイン
    struct EventsMixin {

        // コールバックの登録
        auto& on_construct() { return construct; }
        auto& on_update() { return update; }
        auto& on_destroy() { return destroy; }

        // 利用側（派生クラス）から呼び出す通知用メソッド
        void notify_construct(EntityID entity) { construct(entity); }
        void notify_update(EntityID entity) { update(entity); }
        void notify_destroy(EntityID entity) { destroy(entity); }

    private:
        using CallbackType = EVENT::CallbackList_Single<void(const EntityID)>;

    private:
        EVENT::CallbackList_Single<void(const EntityID)> construct;
        EVENT::CallbackList_Single<void(const EntityID)> update;
        EVENT::CallbackList_Single<void(const EntityID)> destroy;
    };

}//namespace COMPONENT_STORAGE

}//namespace ECS

#endif // !ECS_STORAGEMIXIN_HPP

