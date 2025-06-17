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
public:
    void push_on_construct(const EntityID entt) {
        std::cout<<"push_on_construct()"<< std::endl;
        /*
        if (!elem.contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->contains(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (!cpool->contains(entt) && ...); }, filter)) {
            elem.push(entt);
        }
        */
    }

    void push_on_destroy(const EntityID entt) {
        std::cout << "push_on_destroy()" << std::endl;
        /*
        if (!elem.contains(entt)
            && std::apply([entt](auto *...cpool) { return (cpool->contains(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (0u + ... + cpool->contains(entt)) == 1u; }, filter)) {
            elem.push(entt);
        }
        */
    }

    void remove_if(const EntityID entt){
        std::cout << "remove_if()" << std::endl;
    }
    
public:
    virtual ~Group_handler() = default;

    static constexpr size_t groupCount = Owned + Get + Exclude;

    using StorageType = Type;

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
    handlerType* node() const noexcept {
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
    Group_handler(OGTuple&& ogpool, ETuple&& epool)
        : pools{
        // ogpool 内の各要素は非コピー可能であれば std::apply によって参照を渡す
        std::apply([](auto&... nodePtr)->std::array<handlerType*, sizeof...(OGType)> {
            return { nodePtr.get()... };
        }, std::forward<OGTuple>(ogpool))
    },
        filter{
          std::apply([](auto&... nodePtr)->std::array<handlerType*, sizeof...(EType)> {
              return { nodePtr.get()... };
          }, std::forward<ETuple>(epool))
        }
        {
            // register_events も転送を使って渡す（必要に応じてオーバーロードや参照受け側を調整）
            register_events<&Group_handler::push_on_construct, &Group_handler::remove_if>(std::forward<OGTuple>(ogpool));
            register_events<&Group_handler::remove_if, &Group_handler::push_on_destroy>(std::forward<ETuple>(epool));
        }

         template<typename... OGType, typename... EType>
    group_handler(std::tuple<OGType &...> ogpool, std::tuple<EType &...> epool)
        : pools{std::apply([](auto &&...cpool) { return std::array<common_type *, (Owned + Get)>{&cpool...}; }, ogpool)},
          filter{std::apply([](auto &&...cpool) { return std::array<common_type *, Exclude>{&cpool...}; }, epool)} {
        std::apply([this](auto &...cpool) { ((cpool.on_construct().template connect<&group_handler::push_on_construct>(*this), cpool.on_destroy().template connect<&group_handler::remove_if>(*this)), ...); }, ogpool);
        std::apply([this](auto &...cpool) { ((cpool.on_construct().template connect<&group_handler::remove_if>(*this), cpool.on_destroy().template connect<&group_handler::push_on_destroy>(*this)), ...); }, epool);
        common_setup();
    }

    */
    
    template<typename... OGType, typename... EType>
    Group_handler(std::tuple<OGType&...> ogpool,std::tuple<EType&...> epool)
        : pools{
            std::apply([](auto&&... cpool) {
                   return std::array<handlerType*, sizeof...(OGType)>{ &cpool... };
               }, ogpool)},
        filter{
            std::apply([](auto&&... cpool) {
                 return std::array<handlerType*, sizeof...(EType)>{ &cpool... };
             }, epool)
             }
             {
                register_events<&Group_handler::push_on_construct, &Group_handler::remove_if>(ogpool);
                register_events<&Group_handler::remove_if, &Group_handler::push_on_destroy>(epool);
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

private:
    //std::array<std::unique_ptr<handlerType>, (Owned + Get)> pools;
    //std::array<std::unique_ptr<handlerType>, Exclude> filter;
    std::array<handlerType*, (Owned + Get)> pools;
    std::array<handlerType*, Exclude> filter;
    
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

    Group(handler &ref) noexcept : m_handler(&ref){
    }

    constexpr size_t count() noexcept{
        return m_handler->groupCount;
    }

    bool empty() const noexcept{
        return !*this||!m_handler->len;
    }

    template<typename Type>
    constexpr auto node() noexcept {
        return node<typeIndex<Type>>();
    }

    template<std::size_t Index>
    auto node() const noexcept {
        using element = typename type_list<Owner..., Get..., Exclude...>::template get<Index>;
        return m_handler&&Index!=npos ? static_cast<element*>(m_handler->node<Index>()) : nullptr;
    }

    /*
    template<typename... Owner>
    constexpr std::array<size_t, sizeof...(Owner)> entityCount() noexcept {
        return { storage<typename std::tuple_element_t<typeIndex<Owner>, std::tuple<Owner...>>>()->Size()... };
    }
    */

    /*
    template<typename Type>
    constexpr ISparseSet* emplace()
    */

private:
    handler* m_handler;
};
}//namespace ECS

#endif