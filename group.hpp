#ifndef ECS_GROUP_HPP
#define ECS_GROUP_HPP

#include <type_traits>
#include <utility>
#include <string_view>
#include <type_traits>
#include "Entity.h"
#include "Storage.hpp"
#include "Debug.h"
#include "HashFunctions.h"
#include "typeList.hpp"
#include "World.h"

namespace ECS {
struct IHandler {
public:
    using size_type = std::size_t;
    virtual ~IHandler() = default;
    virtual bool owned(const ecs_map::id_type) const noexcept {
        return false;
    }
};

//データ管理
template<typename Type, std::size_t Owned, std::size_t Get, std::size_t Exclude>
class Group_handler final :public IHandler {

    using entityType = size_t;

    /*
    void push_on_construct(const entity_type entt) {
        if (!elem.contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->contains(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (!cpool->contains(entt) && ...); }, filter)) {
            elem.push(entt);
        }
    }

    void push_on_destroy(const entity_type entt) {
        if (!elem.contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->contains(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (0u + ... + cpool->contains(entt)) == 1u; }, filter)) {
            elem.push(entt);
        }
    }
    */
    
public:
    virtual ~Group_handler() = default;

    static constexpr size_t size = Owned + Get + Exclude;

    size_t length() noexcept{
        return len;
    }

    /*
    template<typename Type>
    auto* storage() const noexcept {
        return storage<index_of<Type>>();
    }
    */

    template<std::size_t Index>
    ISparseSet* storage() const noexcept {
        if(Index == npos){
            return nullptr;
        }

        if constexpr (Index < (Owned + Get)) {
            return pools[Index];
        }
        else {
            return filter[Index - (Owned + Get)];
        }
    }

    /*
    template<typename... OGType, typename... EType>
    Group_handler(std::tuple<OGType &...> ogpool, std::tuple<EType &...> epool)
        : pools{ std::apply([](auto &&...cpool) { return std::array<common_type*, (Owned + Get)>{&cpool...}; }, ogpool) },
        filter{ std::apply([](auto &&...cpool) { return std::array<common_type*, Exclude>{&cpool...}; }, epool) }
    {
        register_events(pools, &group_handler::push_on_construct, &group_handler::remove_if);
        register_events(filter, &group_handler::remove_if, &group_handler::push_on_destroy);
        //common_setUp
    }
    
private:
    template<typename ArrayType, typename ConstructFunc, typename DestroyFunc>
    void register_events(ArrayType& components, ConstructFunc construct, DestroyFunc destroy) {
        for (auto* cpool : components) {
            cpool->on_construct().template connect<construct>(*this);
            cpool->on_destroy().template connect<destroy>(*this);
        }
    }
    */

    

    
    template<typename... OGType, typename... EType>
    Group_handler(std::tuple<OGType &...> ogpool, std::tuple<EType &...> epool)
        : pools{ std::apply([](auto &&...cpool) { return std::array<ISparseSet*, (Owned + Get)>{&cpool...}; }, ogpool) },
        filter{std::apply([](auto &&...cpool) { return std::array<ISparseSet*, Exclude>{&cpool...}; }, epool)}
    {
       
    }
    
private:
    std::array<ISparseSet*, (Owned + Get)> pools;
    std::array<ISparseSet*, Exclude> filter;
    
    size_t len{};
};

template<typename, typename, typename>
class Group;

//機能
template<typename... Owner, typename... Get, typename... Exclude>
class Group<owned_t<Owner...>, get_t<Get...>, exclude_t<Exclude...>> {
    using BaseType = std::common_type_t<typename Owner::BaseType..., typename Get::BaseType..., typename Exclude::BaseType...>;
    using group_type = BaseType;

    //using group_type = std::common_type_t<typename storage_for_t<owner>::base_type..., Get, Exclude;

    template<typename T, typename = void>
    struct has_type : std::false_type {};

    //使用例
    //static_assert(has_type<typename Owner::BaseType>::value, "Owner::BaseType does not have 'type'!");

    

    template<typename T>
    struct has_type<T, std::void_t<typename T::type>> : std::true_type {};

    template<typename Type>
    static constexpr std::size_t typeIndex = type_Index_v<
    std::remove_const_t<Type>,
    type_list<typename Owner::type...,typename Get::type...,typename Exclude::type...>
    >;

public:
    using handler = Group_handler<group_type, sizeof...(Owner), sizeof...(Get), sizeof...(Exclude)>;

    static ecs_map::id_type group_id() noexcept {
        return ecs_map::type_hash<Group<owned_t<std::remove_const_t<Owner>...>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }

    Group() noexcept
        : m_handler{} {}

    Group(handler &ref) noexcept : m_handler(&ref){
    }

    constexpr size_t size() noexcept{
        return m_handler->size;
    }

    const std::vector<size_t> storageSize()
    {
        return m_handler->storageSizes();
    }

    bool empty() const noexcept{
        return !*this||!m_handler->len;
    }

    template<typename Type>
    constexpr ISparseSet* storage() noexcept {
        if(!m_handler) return nullptr;

        return m_handler->storage<typeIndex<Type>>();
    }

    template<typename... Owner>
    constexpr std::array<size_t, sizeof...(Owner)> entityCount() noexcept {
        return { storage<typename std::tuple_element_t<typeIndex<Owner>, std::tuple<Owner...>>>()->Size()... };
    }

    /*
    template<typename Type>
    constexpr ISparseSet* emplace()
    */

private:
    handler* m_handler;
};
}

#endif