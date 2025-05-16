#pragma once
#include "World.h"
#include <type_traits>
#include <utility>

template<typename, typename, typename>
class basic_group;

/**
 * @brief Variable template for lists of owned elements.
 * @tparam Type List of types.
 */
template<typename... Get, typename... Exclude>
class basic_group<owned_t<>, get_t<Get...>, exclude_t<Exclude...>> {

public:
    static id_type group_id() noexcept {
        return type_hash<basic_group<owned_t<>, get_t<std::remove_const_t<Get>...>, exclude_t<std::remove_const_t<Exclude>...>>>::value();
    }
};