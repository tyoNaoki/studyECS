#ifndef ECS_COMPONENTPOOLMANAGER_HPP
#define ECS_COMPONENTPOOLMANAGER_HPP

#include "Storage.hpp"
#include <vector>
#include <memory>
#include <bitset>
#include "HopscotchHashMap.h"

namespace ECS{

namespace COMPONENT{

    // コンポーネントに対するストレージ種別のデフォルトを定義する Trait
    template<class, class = void>
    struct component_storage_selector {
        static constexpr ECS::StorageType value = ECS::StorageType::EventType;   // デフォルト
    };

    template<class T>
    struct component_storage_selector<
        T,
        std::void_t<decltype(T::storage_pref)>
    >
    {
        static constexpr ECS::StorageType value = T::storage_pref;
    };


template <size_t ComponentMaxNum = 64>
class ComponentPoolManager {
    // '1' == active, '0' == inactive.
	using ComponentBitSet = std::bitset<ComponentMaxNum>;

    using ComponentPools = std::vector<std::unique_ptr<ISparseSet>>;

public:
    constexpr size_t maxSize() noexcept {
        return ComponentMaxNum;
    }

    void setEntityMax(const EntityID& id) noexcept{
        m_entityMasks.Set(id,{});
    }

    ComponentBitSet& getEntityMask(EntityID& id) noexcept {
        return *m_entityMasks.Get(id);
    }

    void deleteEntity(const EntityID& id) {
        m_entityMasks.Delete(id);
    }

    void deleteAllComponent(const EntityID& entity) {
        auto& bit = getEntityMask(entity);

        //所持コンポーネントの数
        int compCount = bit.count();

        //全てのコンポーネントデータ削除
        for (int i = 0; i < maxSize(); i++) {
            if (bit[i] == 1) {
                if (deleteComponent(i, entity)) {
                    compCount--;

                    if (compCount <= 0) break;
                }
            }
        }
    }

    bool deleteComponent(size_t compIndex,const EntityID& entity){
        if(m_componentPools[compIndex]){
            m_componentPools[compIndex]->Delete(entity);
            return true;
        }

        return false;
    }

    template <typename T, typename... Args>
    T* emplace(const EntityID& entityID, Args&&... args) {
        auto& pool = getComponentPool<T>();

        //グループ更新
        registComponentSet<T>(entityID);

        return pool.Set(entityID, std::move(T{ std::forward<Args>(args)... }));
    }

    template <typename T>
    T* getComponent(const EntityID& entityID) {
        auto& pool = getComponentPool<T>();
        return pool.Get(entityID);
    }

    template <typename T>
    void removeComponent(const EntityID& entityID) {
        //無効なEntity
        if (entityID == INVALID_ENTITY) return;

        auto& pool = getComponentPool<T>();

        if (!pool.Get(entityID)) return;

        pool.Delete(entityID);

        //グループ更新
        ComponentBitSet& mask = getEntityMask(entityID);
        setComponentBit<T>(mask, 0);
    }

    ComponentBitSet* getComponentBitSet(const EntityID& entity) {
        return m_entityMasks.Get(entity);
    }

    template <typename... Components>
    bool has(EntityID entity) {
        auto bitset = getEntityMask(entity);

        if (!bitset.any()) return false;

        ComponentBitSet newMask = getMask<Components...>();
        return ((bitset & newMask) == newMask);
    }

    template <typename T>
    auto& getComponentPool() {
        ISparseSet* genericPtr = getComponentPoolPtr<T>();
        constexpr auto S = component_storage_selector<T>::value;
        auto* pool = static_cast<StorageClass_t<T, S>*>(genericPtr);
        return *pool;
    };

    template <typename T>
    ISparseSet* getComponentPoolPtr() {
        size_t index = getOrRegisterComponentIndex<T>();
        return m_componentPools[index].get();
    };

    template <typename... Components>
    void registComponentSet(const EntityID& entity) {
        ComponentBitSet* bitset = m_entityMasks.Get(entity);

        //無効なentityを指定した
        if (!bitset) {
            return;
        }

        ComponentBitSet newMask = getMask<Components...>();

        // すべてのコンポーネントがすでに登録されている場合は何もしない
        if ((*bitset & newMask) == newMask) {
            return;
        }

        //コンポーネント登録
        std::initializer_list<int>{(setComponentBit<Components>(*bitset, 1), 0)... };
    }

private:

    template <typename T>
    void registerComponent() {
        ASSERT(m_componentPools.size() <= ComponentMaxNum,
            "Exceeded max number of registered components");

        size_t index = getComponentIndex<T>();

        if (index >= m_componentPools.size())
            m_componentPools.resize(index + 1);

        ASSERT(!m_componentPools[index],
            "Attempting to register component '" << typeid(T).name() << "' twice");

        constexpr auto storageType = component_storage_selector<T>::value;

        m_componentPools[index] = std::make_unique<StorageClass_t<T, storageType>>();

        CUSTOM_INFO("Registered component '" << typeid(T).name() << "'");
    };

    static size_t getNextComponentIndex(const std::string typeName)
    {
        static size_t ind = 0;

        if (ind > ComponentMaxNum)
        {
            ASSERT(false, typeName << " Component index over MAX_COMPONENTS " << ComponentMaxNum);
        }

        return ind++;
    };

    template <typename T>
    static size_t getComponentIndex() {
        static size_t ind = getNextComponentIndex(typeid(T).name());
        return ind;
    };

    template <typename T>
    typename ComponentBitSet::reference getComponentBit(ComponentBitSet& mask) {
        size_t bitPos = getComponentIndex<T>();
        return mask[bitPos];
    }

    template <typename... Components>
    ComponentBitSet getMask() {
        ComponentBitSet mask;
        std::initializer_list<int>{ (setComponentBit<Components>(mask, 1), 0)... };
        return mask;
    }

    ComponentBitSet& getEntityMask(EntityID entity) {
        return *m_entityMasks.Get(entity);
    }

   

    template <typename T>
    void setComponentBit(ComponentBitSet& bit, bool val) {
        size_t bitPos = getComponentIndex<T>();
        bit[bitPos] = val;
    }

    template <typename T>
    size_t getOrRegisterComponentIndex() {
        size_t index = getComponentIndex<T>();

        if (index >= m_componentPools.size() || !m_componentPools[index])
            registerComponent<T>();

        // Internal error, should never happen outside development
        ASSERT(index < m_componentPools.size() && index >= 0,
            "Type index out of bounds for component '" << typeid(T).name() << "'");

        return index;
    };

private:
	std::vector<std::unique_ptr<ISparseSet>> m_componentPools;

	SparseSet<ComponentBitSet>m_entityMasks;
};

}//namespace COMPONENT

}//namespace ECS

#endif
