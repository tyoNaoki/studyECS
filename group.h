#pragma once
#include "World.h"
#include <type_traits>
#include <utility>
#include <string_view>
#include <type_traits>

template<typename T>
constexpr std::string_view stripped_type_name() noexcept {
#ifdef __clang__
    constexpr std::string_view pretty_function = __PRETTY_FUNCTION__;
#elif defined(__GNUC__)
    constexpr std::string_view pretty_function = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    constexpr std::string_view pretty_function = __FUNCSIG__;
#else
    static_assert(false, "Unsupported compiler: Define ENTT_PRETTY_FUNCTION for this environment");
    return "UnknownType";
#endif

    constexpr size_t prefix_length = pretty_function.find_first_of(' ') + 1;
    constexpr size_t suffix_length = pretty_function.find_last_of(']') - prefix_length;

    return pretty_function.substr(prefix_length, suffix_length);
}

using id_type = std::size_t;

constexpr id_type hash_Create(std::string_view sv){
    id_type hash = 0xcbf29ce484222325; // FNV-1a ハッシュの初期値
    constexpr id_type prime = 0x100000001b3; // FNV-1a の乗算値

    for (char c : sv) {
        hash ^= static_cast<id_type>(c);
        hash *= prime;
    }

    return hash;
}

template<typename Type>
constexpr id_type type_hash() noexcept {
    return hash_Create(stripped_type_name<Type>());
}

template<typename, typename, typename>
class basic_group;

template<typename owner,typename... Get, typename... Exclude>
class basic_group<owned_t<owner>, get_t<Get...>, exclude_t<Exclude...>> {

public:
    static id_type group_id() noexcept {
        return type_hash<basic_group<owned_t<owner>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>();
    }
};