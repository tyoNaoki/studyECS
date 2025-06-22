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

    using entityType = EntityID;
    using handlerType = Type;

    void swap_elements(const std::size_t pos, const EntityID entt) {
        for (size_type next{}; next < Owned; ++next) {
            std::cout<< GetEntityIndex(pools[next]->GetEntity(pos))-1 << " swap elements " << GetEntityIndex(entt)-1 << std::endl;
            pools[next]->swap_elements(pools[next]->GetEntity(pos), entt);
        }
    }
    
    void push_on_construct(const EntityID entt) {
        std::cout << "push_on_construct() : " << GetEntityIndex(entt)-1 << std::endl;
        if (std::apply([entt, pos = len](auto* cpool, auto *...other) { return cpool->ContainsEntity(entt) && !(cpool->Index(entt) < pos) && (other->ContainsEntity(entt) 
            && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (!cpool->ContainsEntity(entt) && ...); }, filter)) {
            swap_elements(len++, entt);
        }
    }

    void push_on_destroy(const EntityID entt) {
        std::cout << "push_on_destroy() : " << GetEntityIndex(entt) << " entity" << std::endl;

        if (std::apply([entt, pos = len](auto* cpool, auto *...other) { return cpool->ContainsEntity(entt) && !(cpool->Index(entt) < pos) && (other->ContainsEntity(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (0u + ... + cpool->ContainsEntity(entt)) == 1u; }, filter)) {
            swap_elements(len++, entt);
        }
    }

    void remove_if(const EntityID entt){
        std::cout << "remove_if() : " << GetEntityIndex(entt) << " entity" << std::endl;

        if (pools[0u]->ContainsEntity(entt) && (pools[0u]->Index(entt) < len)) {
            swap_elements(--len, entt);
        }
    }

public:
    virtual ~Group_handler() = default;

    static constexpr size_t groupCount = Owned + Get + Exclude;

    using StorageType = Type;

    size_t length() noexcept{
        return len;
    }

    template<std::size_t Index>
    handlerType* getComponentPool() const noexcept {
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

    bool owned(const ecs_map::id_type id) const noexcept override{
        for (size_type pos{}; pos < Owned; ++pos) {
            if (pools[pos]->Hash() == id) {
                return true;
            }
        }

        return false;
    }
    
    template<typename... OGType, typename... EType>
    Group_handler(std::tuple<OGType&...> ogpool,std::tuple<EType&...> epool)
        : pools{
            std::apply([](auto&&... cpool) {
                   return std::array<handlerType*, sizeof...(OGType)>{ &cpool... };
               }, ogpool)},
        filter{
            std::apply([](auto&&... cpool) {
                 return std::array<handlerType*, sizeof...(EType)>{ &cpool... };
            }, epool)}
    {
        register_events<&Group_handler::push_on_construct, &Group_handler::remove_if>(ogpool);
        register_events<&Group_handler::remove_if, &Group_handler::push_on_destroy>(epool);
        first_events();
    }
    
private:
    template<auto Construct, auto Destroy, typename ArrayType>
    void register_events(ArrayType& componentPools) {
        std::apply([this](auto&... componentPool) {
            ((
                componentPool.on_construct().template append<Construct>(*this),
                componentPool.on_destroy().template append<Destroy>(*this)
                ), ...);
        }, componentPools);
    }

    void first_events() {
        for (auto first = pools[0u]->begin(), last = first + static_cast<std::ptrdiff_t>(pools[0u]->Size()); first != last; ++first) {

            push_on_construct(*first);
        }
    }

private:
    //std::array<std::unique_ptr<handlerType>, (Owned + Get)> pools;
    //std::array<std::unique_ptr<handlerType>, Exclude> filter;
    std::array<handlerType*, (Owned + Get)> pools;
    std::array<handlerType*, Exclude> filter;
    
    size_t len{};
};

//データ管理
template<typename Type, std::size_t Get, std::size_t Exclude>
class Group_handler<Type, 0u, Get, Exclude> final :public IHandler {

    using entityType = EntityID;
    using handlerType = Type;

    void push_on_construct(const EntityID entt) {
        if (!contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->ContainsEntity(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (!cpool->ContainsEntity(entt) && ...); }, filter)) {
            elem.push_back(entt);
        }
    }

    void push_on_destroy(const EntityID entt) {
        if (!contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->ContainsEntity(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (0u + ... + cpool->ContainsEntity(entt)) == 1u; }, filter)) {
            elem.push_back(entt);
        }
    }

    void remove_if(const EntityID entt) {
        auto newEnd = remove(elem.begin(), elem.end(), entt);
        elem.erase(newEnd, elem.end());
    }


public:
    virtual ~Group_handler() = default;

    using StorageType = Type;

    template<typename... GType, typename... EType>
    Group_handler(std::tuple<GType&...> gpool, std::tuple<EType&...> epool)
        : pools{std::apply([](auto&&... cpool) {return std::array<handlerType*, sizeof...(GType)>{ &cpool... }; }, gpool) },
          filter{std::apply([](auto&&... cpool) { return std::array<handlerType*, sizeof...(EType)>{ &cpool... };}, epool) }{ 

        register_events<&Group_handler::push_on_construct, &Group_handler::remove_if>(gpool);
        register_events<&Group_handler::remove_if, &Group_handler::push_on_destroy>(epool);

        first_events();
    }

    std::vector<EntityID>& handle() noexcept {
        return elem;
    }

    const std::vector<EntityID>& handle() const noexcept {
        return elem;
    }

    template<std::size_t Index>
    handlerType* getComponentPool() const noexcept {
        if (Index == npos) {
            return nullptr;
        }

        if constexpr (Index < Get) {
            return pools[Index];
        }
        else {
            return filter[Index - Get];
        }
    }

private:
    bool contains(const EntityID& entt) const {
        return std::find(elem.begin(), elem.end(), entt) != elem.end();
    }

    template<auto Construct, auto Destroy, typename ArrayType>
    void register_events(ArrayType& componentPools) {
        std::apply([this](auto&... componentPool) {
            ((
                componentPool.on_construct().template append<Construct>(*this),
                componentPool.on_destroy().template append<Destroy>(*this)
                ), ...);
            }, componentPools);
    }

    void first_events() {
        for (auto first = pools[0u]->begin(), last = first + static_cast<std::ptrdiff_t>(pools[0u]->Size()); first != last; ++first) {
            push_on_construct(*first);
        }
    }

private:
    //std::array<std::unique_ptr<handlerType>, (Owned + Get)> pools;
    //std::array<std::unique_ptr<handlerType>, Exclude> filter;
    std::array<handlerType*, Get> pools;
    std::array<handlerType*, Exclude> filter;
    std::vector<EntityID> elem;
};

template<typename, typename, typename>
class Group;

//機能
template<typename... Get, typename... Exclude>
class Group<owned_t<>, get_t<Get...>, exclude_t<Exclude...>> {
    using BaseType = std::common_type_t<typename Get::BaseType..., typename Exclude::BaseType...>;

    using group_type = BaseType;

    //using group_type = std::common_type_t<typename storage_for_t<owner>::base_type..., Get, Exclude;

    //使用例
    //static_assert(has_type<typename Owner::BaseType>::value, "Owner::BaseType does not have 'type'!");

    template<typename T, typename = void>
    struct has_type : std::false_type {};

    template<typename T>
    struct has_type<T, std::void_t<typename T::type>> : std::true_type {};

    template<typename Type>
    static constexpr std::size_t typeIndex = type_Index_v<
        std::remove_const_t<Type>,
        type_list<typename Get::type..., typename Exclude::type...>
    >;

public:
    using handler = Group_handler<BaseType, 0u, sizeof...(Get), sizeof...(Exclude)>;

    static ecs_map::id_type group_id() noexcept {
        return ecs_map::type_hash<Group<owned_t<>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }

    Group() noexcept
        : m_handler{} {}

    Group(handler& ref) noexcept : m_handler(&ref) {
    }

    constexpr size_t count() noexcept {
        return m_handler->groupCount;
    }

    bool empty() const noexcept {
        return !*this || !m_handler->len;
    }

    template<typename Type>
    auto getComponentPool() noexcept {
        return getComponentPool<typeIndex<Type>>();
    }

    template<std::size_t Index>
    auto getComponentPool() noexcept {
        using element = typename type_list<Get..., Exclude...>::template get<Index>;
        return (m_handler && Index != npos) ? static_cast<element*>(m_handler->getComponentPool<Index>()) : nullptr;
    }

private:
    handler* m_handler;
};

//機能
template<typename... Owner, typename... Get, typename... Exclude>
class Group<owned_t<Owner...>, get_t<Get...>, exclude_t<Exclude...>> {
    using BaseType = std::common_type_t<typename Owner::BaseType..., typename Get::BaseType..., typename Exclude::BaseType...>;

    using group_type = BaseType;

    //using group_type = std::common_type_t<typename storage_for_t<owner>::base_type..., Get, Exclude;

    //使用例
    //static_assert(has_type<typename Owner::BaseType>::value, "Owner::BaseType does not have 'type'!");

    template<typename T, typename = void>
    struct has_type : std::false_type {};

    template<typename T>
    struct has_type<T, std::void_t<typename T::type>> : std::true_type {};

    template<typename Type>
    static constexpr std::size_t typeIndex = type_Index_v<
    std::remove_const_t<Type>,
    type_list<typename Owner::type...,typename Get::type...,typename Exclude::type...>
    >;

public:
    using handler = Group_handler<BaseType, sizeof...(Owner), sizeof...(Get), sizeof...(Exclude)>;

    static ecs_map::id_type group_id() noexcept {
        return ecs_map::type_hash<Group<owned_t<std::remove_const_t<Owner>...>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }

    Group() noexcept
        : m_handler{} {}

    Group(handler &ref) noexcept : m_handler(&ref){}

    constexpr size_t count() noexcept{
        return m_handler->groupCount;
    }

    bool empty() const noexcept{
        return !*this||!m_handler->len;
    }

    template<typename Type>
    auto getComponentPool() noexcept {
        return getComponentPool<typeIndex<Type>>();
    }

    template<std::size_t Index>
    auto getComponentPool() noexcept {
        using element = typename type_list<Owner..., Get..., Exclude...>::template get<Index>;
        return (m_handler&&Index!=npos) ? static_cast<element*>(m_handler->getComponentPool<Index>()) : nullptr;
    }

private:
    handler* m_handler;
};
}//namespace ECS

#endif