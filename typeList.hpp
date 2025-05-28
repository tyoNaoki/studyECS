#ifndef ECS_TYPELIST_HPP
#define ECS_TYPELIST_HPP

#include<iostream>
#include<tuple>
#include <type_traits>

namespace ECS{

template<typename... Type>
struct type_list {
    
    /*! @brief Type list type. */
    using type = type_list;
    static constexpr auto size = sizeof...(Type);

    using type_tuple = std::tuple<Type...>;

    // インデックスを指定して型を取得（範囲外チェック付き）
    template<std::size_t Index>
    using get = std::conditional_t<
        (Index < size),
        std::tuple_element_t<Index, type_tuple>,
        void  // 範囲外の場合は void を返す
        >;

    /*! @brief Compile-time number of elements in the type list. */
   
};

//型が見つからなかった
constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

template<typename T, typename List>
struct type_Index;

// 非空の型リストの場合の特殊化
template<typename T, typename First, typename... Rest>
struct type_Index<T, type_list<First, Rest...>> {
private:
    // 次の部分のインデックスを再帰的に計算
    static constexpr std::size_t next = type_Index<T, type_list<Rest...>>::value;
public:
    // 最初の型と一致するなら 0、一致しなければ次の値＋1
    // 一致する型が見つからなければ npos を伝搬します。
    static constexpr std::size_t value = std::is_same_v<T, First> ? 0 : (next == npos ? npos : 1 + next);
};

// 空の型リストの場合：対象の型が存在しないので npos を返す
template<typename T>
struct type_Index<T, type_list<>> {
    static constexpr std::size_t value = npos;
};

//型確認
template<typename T, typename List>
inline constexpr std::size_t type_Index_v = type_Index<T, List>::value;

/**
使用例
using MyList = type_list<int, double, char>;
static_assert(type_Index<int, MyList>::value == 0, "int should be at index 0");
static_assert(type_Index<double, MyList>::value == 1, "double should be at index 1");
static_assert(type_Index<char, MyList>::value == 2, "char should be at index 2");
static_assert(type_Index<float, MyList>::value == static_cast<std::size_t>(-1), "float is not in the list");
**/

template<typename... Type>
struct exclude_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit exclude_t() = default;
};

template<>
struct exclude_t<> final : type_list<> {
    explicit exclude_t() = default;
};

/**
 * @brief Variable template for exclusion lists.
 * @tparam Type List of types.
 */
template<typename... Type>
inline constexpr exclude_t<Type...> exclude{};

/**
 * @brief Alias for lists of observed elements.
 * @tparam Type List of types.
 */
template<typename... Type>
struct get_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit get_t() = default;
};

/**
 * @brief Variable template for lists of observed elements.
 * @tparam Type List of types.
 */
template<typename... Type>
inline const get_t<Type...> get{};  // `inline constexpr` → `static const`

/**
 * @brief Alias for lists of owned elements.
 * @tparam Type List of types.
 */
template<typename... Type>
struct owned_t final : type_list<Type...> {
    /*! @brief Default constructor. */
    explicit owned_t() = default;
};

/**
 * @brief Variable template for lists of owned elements.
 * @tparam Type List of types.
 */
template<typename... Type>
inline const owned_t<Type...> owned{};  // `inline constexpr` → `static const`
}
#endif