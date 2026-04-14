#ifndef ECS_HASHFUNCTIONS_HPP
#define ECS_HASHFUNCTIONS_HPP

#include <iostream>

/*
// ハッシュアルゴリズムの定数
    static constexpr uint64_t MASK30 = (1ULL << 30) - 1;
    static constexpr uint64_t MASK31 = (1ULL << 31) - 1;
    static constexpr uint64_t MOD = (1ULL << 61) - 1;
    static constexpr uint64_t MASK61 = MOD;

    static_assert(MOD < UINT64_MAX, "MOD exceeds 64-bit limit");
    static_assert(MASK61 == MOD, "MASK61 should be equal to MOD");

// mod 2^61-1 を計算
    constexpr uint64_t CalcMod(uint64_t x) noexcept {
        uint64_t xu = x >> 61;
        uint64_t xd = x & MASK61;
        uint64_t res = xu + xd;

        return (res >= MOD) ? res - MOD : res;
    }

    // 乗算 mod 2^61-1
    constexpr uint64_t Mul(uint64_t a, uint64_t b) noexcept {
        uint64_t au = a >> 31;
        uint64_t ad = a & MASK31;
        uint64_t bu = b >> 31;
        uint64_t bd = b & MASK31;

        uint64_t mid = ad * bu + au * bd;
        uint64_t midu = mid >> 30;
        uint64_t midd = mid & MASK30;

        return CalcMod(au * bu * 2 + midu + (midd << 31) + ad * bd);
    }

    // カスタムハッシュ関数
    constexpr uint64_t CustomHash(uint64_t key) noexcept {
        constexpr uint64_t base = 31;
        return CalcMod(Mul(base, key));
    }


*/

namespace ECS{

namespace ecs_map{

    template<typename T>
    inline constexpr std::string_view stripped_type_name() noexcept {
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
    
    using id_type = std::conditional_t<sizeof(void*) == 8, uint64_t, uint32_t>;

    inline constexpr uint32_t FNV1a32(std::string_view sv) noexcept {
        uint32_t hash = 0x811C9DC5; // FNV-1a 初期値 (32-bit)
        constexpr uint32_t prime = 0x01000193;

        for (char c : sv) {
            hash ^= static_cast<uint32_t>(c);
            hash *= prime;
        }

        return hash;
    }

    inline constexpr uint64_t FNV1a64(std::string_view sv) noexcept {
        uint64_t hash = 0xCBF29CE484222325ULL; // FNV-1a 初期値 (64-bit)
        constexpr uint64_t prime = 0x100000001B3ULL;

        for (char c : sv) {
            hash ^= static_cast<uint64_t>(c);
            hash *= prime;
        }

        return hash;
    }

    inline constexpr id_type FNV1aHash(std::string_view sv) {
        if constexpr (std::is_same_v<id_type, uint64_t>) {
            return FNV1a64(sv);
        }
        else {
            return FNV1a32(sv);
        }
    }

    template<typename Type>
    inline constexpr id_type type_hash() noexcept {
        using Stripped = std::remove_cv_t<Type>;
        return FNV1aHash(stripped_type_name<Stripped>());
    }

}//namespace ecs_map
}//namespace ECS

#endif // !ECS_HASHFUNCTIONS_HPP