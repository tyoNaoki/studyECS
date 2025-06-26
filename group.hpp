#ifndef ECS_GROUP_HPP
#define ECS_GROUP_HPP

#include <type_traits>
#include <utility>
#include <string_view>
#include <type_traits>
#include "Entity.h"
#include "Storage.hpp"
#include "Debug.h"
#include "HashFunctions.hpp"
#include "typeList.hpp"
#include "World.h"
#include "SparseSet.h"

namespace ECS {

    //------------------------------------------------------------------------------
    // input_iterator_pointer<T>
    //   - value_type:  T
    //   - reference:   T&
    //   - pointer:     T*
    //   - ctor:        T をムーブして内部に保持
    //   - operator*(): T& を返す
    //   - operator->(): T* を返す
    //------------------------------------------------------------------------------
    template<typename T>
    struct input_iterator_pointer {
        using value_type = T;
        using reference = T&;
        using pointer = T*;

    private:
        T _value;  // operator*() で返す実体

    public:
        // ムーブコンストラクタで初期化
        explicit constexpr input_iterator_pointer(value_type&& v)
            noexcept(std::is_nothrow_move_constructible_v<value_type>)
            : _value(std::move(v))
        {}

        // *proxy -> value_type&
        [[nodiscard]]
        constexpr reference operator*() noexcept {
            return _value;
        }

        // proxy->member -> &(proxy._value)
        [[nodiscard]]
        constexpr pointer operator->() noexcept {
            return std::addressof(_value);
        }
    };


template<typename,typename,typename>
class group_iterator;

template<typename It,typename... Owned,typename... Get>
class group_iterator<It,owned_t<Owned...>,get_t<Get...>>{
    template<typename Type>
    auto index_to_element(Type& cpool) const {
        if constexpr (std::is_void_v<typename Type::value_type>) {
            return std::make_tuple();
        }
        else {
            auto& values = cpool.GetValues();    // ← values() は vector<T>& を返す想定
            return std::forward_as_tuple(
                values.rbegin()[it.index()]
            );

            //return std::forward_as_tuple(cpool.rbegin()[it.index()]);
        }
    }

public:
    using iterator_type = It;
    
    using value_type = decltype(std::tuple_cat(std::make_tuple(*std::declval<It>()), std::declval<Owned>().GetRef_as_tuple({})..., std::declval<Get>().GetRef_as_tuple({})...));

    using pointer = input_iterator_pointer<value_type>;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr group_iterator()
        : it{},
        pools{} {}

    group_iterator(iterator_type from, std::tuple<Owned *..., Get *...> cpools)
        : it{ from },
        pools{ std::move(cpools) } {}

    group_iterator& operator++() noexcept {
        return ++it, * this;
    }

    group_iterator operator++(int) noexcept {
        const group_iterator orig = *this;
        return ++(*this), orig;
    }

    reference operator*() const noexcept {
        return std::tuple_cat(std::make_tuple(*it), index_to_element(*std::get<Owned*>(pools))..., std::get<Get*>(pools)->GetRef_as_tuple(*it)...);
    }

    pointer operator->() const noexcept {
        return input_iterator_pointer<value_type>{operator*()};
    }

    constexpr iterator_type base() const noexcept {
        return it;
    }

    template<typename... Lhs, typename... Rhs>
    friend constexpr bool operator==(const group_iterator<Lhs...>&, const group_iterator<Rhs...>&) noexcept;

private:
    It it;
    std::tuple<Owned *..., Get *...> pools;
};

template<typename... Lhs, typename... Rhs>
constexpr bool operator==(const group_iterator<Lhs...>& lhs, const group_iterator<Rhs...>& rhs) noexcept {
    return lhs.it == rhs.it;
}

template<typename... Lhs, typename... Rhs>
constexpr bool operator!=(const group_iterator<Lhs...>& lhs, const group_iterator<Rhs...>& rhs) noexcept {
    return !(lhs == rhs);
}

template<typename It, typename Sentinel = It>
struct iterable_adapter {
    It        first;
    Sentinel  last;

    constexpr iterable_adapter(It f, Sentinel l) noexcept
        : first(f), last(l)
    {}

    constexpr It begin()   const noexcept { return first; }
    constexpr auto end()   const noexcept { return last; }
    constexpr It cbegin()  const noexcept { return begin(); }
    constexpr auto cend()  const noexcept { return end(); }
};

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
            pools[next]->swap_elements(pools[next]->GetEntity(pos), entt);
        }
    }
    
    void push_on_construct(const EntityID entt) {
        //std::cout << "push_on_construct() : " << GetEntityIndex(entt) << std::endl;
        if (std::apply([entt, pos = len](auto* cpool, auto *...other) { return cpool->ContainsEntity(entt) && !(cpool->Index(entt) < pos) && (other->ContainsEntity(entt) 
            && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (!cpool->ContainsEntity(entt) && ...); }, filter)) {
            swap_elements(len++, entt);
        }
    }

    void push_on_destroy(const EntityID entt) {
        //std::cout << "push_on_destroy() : " << GetEntityIndex(entt) << " entity" << std::endl;

        if (std::apply([entt, pos = len](auto* cpool, auto *...other) { return cpool->ContainsEntity(entt) && !(cpool->Index(entt) < pos) && (other->ContainsEntity(entt) && ...); }, pools)
            && std::apply([entt](auto *...cpool) { return (0u + ... + cpool->ContainsEntity(entt)) == 1u; }, filter)) {
            swap_elements(len++, entt);
        }
    }

    void remove_if(const EntityID entt){
        //std::cout << "remove_if() : " << GetEntityIndex(entt) << " entity" << std::endl;

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
    using common_type = std::common_type_t<typename Get::BaseType..., typename Exclude::BaseType...>;
    using group_type = common_type;

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

    template<std::size_t... Index>
    auto getComponentPools(std::index_sequence<Index...>)const noexcept{
        using return_type = std::tuple<Get *...>;
        return m_handler ? return_type{ static_cast<Get*>(m_handler->template getComponentPool<Index>())... } : return_type{};
    }

public:
    using handler = Group_handler<common_type, 0u, sizeof...(Get), sizeof...(Exclude)>;
    using base_iterator = std::vector<EntityID>::const_iterator;
    using iterator = group_iterator<base_iterator, owned_t<>, get_t<Get...>>;
    using iterable = iterable_adapter<iterator>;

    static ecs_map::id_type group_id() noexcept {
        return ecs_map::type_hash<Group<owned_t<>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }

    Group() noexcept
        : m_handler{} {}

    Group(handler& ref) noexcept : m_handler(&ref) {
    }

    const std::vector<EntityID>& handle()const noexcept{
        return m_handler->handle();
    }

    size_t size() const noexcept{
        return m_handler ? handle().size() : size_t {};
    }

    constexpr size_t count() const noexcept {
        return m_handler->groupCount;
    }

    bool empty() const noexcept {
        return !m_handler || !m_handler->len;
    }

    base_iterator begin() const noexcept {
        return m_handler ? handle().begin() : base_iterator{};
    }

    base_iterator end() const noexcept {
        return m_handler ? handle().end() : base_iterator{};
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

    bool contains(const EntityID entt)const noexcept{
        return m_handler && handle().contains(entt);
    }

    template<typename Type, typename... Other>
    decltype(auto) get(const EntityID entt) const {
        return get<typeIndex<Type>, typeIndex<Other>...>(entt);
    }

    template<std::size_t... Index>
    decltype(auto) get(const EntityID entt) const {
        const auto pools = getComponentPools(std::index_sequence_for<Get...>{});

        if constexpr(sizeof...(Index) == 0){
            return  std::apply([entt](auto *...curr) { return std::tuple_cat(curr->GetRef_as_tuple(entt)...); }, pools);
        }else if constexpr(sizeof...(Index) == 1){
            return (std::get<Index>(pools)->GetRef(entt), ...);
        }else{
            return std::tuple_cat(std::get<Index>(pools)->GetRef_as_tuple(entt)...);
        }
    }

    template<typename Func>
    void each(Func func) const {
        for (const auto entt : *this) {
            if constexpr (is_applicable_v < Func, decltype(std::tuple_cat(std::tuple<EntityID>{}, std::declval<Group>().get({}))) > ) {
                std::apply(func, std::tuple_cat(std::make_tuple(entt), get(entt)));
            }
            else {
                std::apply(func, get(entt));
            }
        }
    }

    iterable each() noexcept{
        const auto pools = getComponentPools(std::index_sequence_for<Get...>{});

        iterator first{
           begin(), pools
        };

        iterator last{
           end(), pools
        };

        return {first,last};
    }

private:
    handler* m_handler;
};

//機能
template<typename... Owner, typename... Get, typename... Exclude>
class Group<owned_t<Owner...>, get_t<Get...>, exclude_t<Exclude...>> {
    using common_type = std::common_type_t<typename Owner::BaseType..., typename Get::BaseType..., typename Exclude::BaseType...>;

    using group_type = common_type;

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

    template<std::size_t... Index,std::size_t... Other>
    auto getComponentPools(std::index_sequence<Index...>,std::index_sequence<Other...>)const noexcept {
        using return_type = std::tuple<Owner *...,Get *...>;
        return m_handler ? return_type{ static_cast<Owner *>(m_handler->template getComponentPool<Index>())...,static_cast<Get *>(m_handler->template getComponentPool<sizeof...(Owner) + Other>())...} : return_type{};
    }

public:
    using handler = Group_handler<common_type, sizeof...(Owner), sizeof...(Get), sizeof...(Exclude)>;
    using base_iterator = typename common_type::iterator;
    using reverse_iterator = typename common_type::reverse_iterator;
    using iterator = group_iterator<base_iterator, owned_t<Owner...>, get_t<Get...>>;
    using iterable = iterable_adapter<iterator>;

    static ecs_map::id_type group_id() noexcept {
        return ecs_map::type_hash<Group<owned_t<std::remove_const_t<Owner>...>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }

    Group() noexcept
        : m_handler{} {}

    Group(handler &ref) noexcept : m_handler(&ref){}

    base_iterator begin() const noexcept {
        return m_handler ? (handle().end() - static_cast<std::ptrdiff_t>(m_handler->length())) : base_iterator{};
    }

    base_iterator end() const noexcept {
        return m_handler ? handle().end() : base_iterator{};
    }

    reverse_iterator rbegin() const noexcept {
        return m_handler ? handle().rbegin() : reverse_iterator{};
    }

    reverse_iterator rend() const noexcept {
        return m_handler ? (handle().rbegin() + static_cast<std::ptrdiff_t>(m_handler->length())) : reverse_iterator{};
    }

    EntityID front() const noexcept {
        const auto it = begin();
        return it != end() ? *it : NULL_INDEX;
    }

    EntityID back() const noexcept {
        const auto it = rbegin();
        return it != rend() ? *it : NULL_INDEX;
    }

    iterator find(const EntityID entt) const noexcept {
        const auto it = m_handler ? handle().find(entt) : iterator{};
        return it >= begin() ? it : iterator{};
    }

    EntityID operator[](const size_t pos) const {
        return begin()[static_cast<std::ptrdiff_t>(pos)];
    }

    explicit operator bool() const noexcept {
        return m_handler != nullptr;
    }

  
    bool contains(const EntityID entt) const noexcept {
        return m_handler && handle().contains(entt) && (handle().index(entt) < (m_handler->length()));
    }

    constexpr size_t count() noexcept{
        return m_handler->groupCount;
    }

    bool empty() const noexcept{
        return !*this||!m_handler->len;
    }

    const common_type *handle() const noexcept{
        return getComponentPool<0>();
    }

    template<typename Type>
    auto *getComponentPool() noexcept {
        return getComponentPool<typeIndex<Type>>();
    }

    template<std::size_t Index>
    auto *getComponentPool() noexcept {
        using element = typename type_list<Owner..., Get..., Exclude...>::template get<Index>;

        if constexpr (Index != npos) {
            return static_cast<element*>(m_handler->getComponentPool<Index>());
        }
        else {
            return nullptr;
        }

    }

    /**
     * @brief Returns the elements assigned to the given entity.
     * @tparam Type Type of the element to get.
     * @tparam Other Other types of elements to get.
     * @param entt A valid identifier.
     * @return The elements assigned to the entity.
     */
    template<typename Type, typename... Other>
    decltype(auto) get(const EntityID entt) const {
        return get<typeIndex<Type>, typeIndex<Other>...>(entt);
    }

    template<std::size_t... Index>
    decltype(auto) get(const EntityID entt) const {
        const auto pools = getComponentPools(std::index_sequence_for<Owner...>{}, std::index_sequence_for<Get...>{});

        if constexpr (sizeof...(Index) == 0) {
            return std::apply([entt](auto *...curr) { return std::tuple_cat(curr->GetRef_as_tuple(entt)...); }, pools);
        }
        else if constexpr (sizeof...(Index) == 1) {
            return (std::get<Index>(pools)->GetRef(entt), ...);
        }
        else {
            return std::tuple_cat(std::get<Index>(pools)->GetRef_as_tuple(entt)...);
        }
    }

    template<typename Func>
    void each(Func func) const {

        for (auto args : each()) {
            if constexpr (is_applicable_v < Func, decltype(std::tuple_cat(std::tuple<EntityID>{}, std::declval<Group>().get({}))) > ) {
                std::apply(func, args);
            }
            else {
                std::apply([&func](auto, auto &&...less) { func(std::forward<decltype(less)>(less)...); }, args);
            }
        }
    }

    iterable each() const noexcept {
        const auto pools = getComponentPools(std::index_sequence_for<Owner...>{}, std::index_sequence_for<Get...>{});

        iterator first{
           begin(), pools
        };

        iterator last{
           end(), pools
        };

        return { first,last };
    }

private:
    handler* m_handler;
};

}//namespace ECS

#endif