#pragma once
#include <array>
#include <cstdint>
namespace breinput::detail {
// HID page 0x07 -> Linux evdev code (also used by the portal keyboard API).
inline constexpr auto kEvdev = [] {
    std::array<std::uint16_t, 232> map{};
    constexpr std::uint16_t letters[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                         49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    for (int i = 0; i < 26; ++i) {
        map[4 + i] = letters[i];
    }
    for (int i = 0; i < 9; ++i) {
        map[30 + i] = 2 + i;
    }
    map[39] = 11;
    constexpr std::uint16_t rest[] = {28,  1,   14,  15,  57, 12, 13,  26,  27,  43,  86,  39,  40,
                                      41,  51,  52,  53,  58, 59, 60,  61,  62,  63,  64,  65,  66,
                                      67,  68,  87,  88,  99, 70, 119, 110, 102, 104, 111, 107, 109,
                                      106, 105, 108, 103, 69, 98, 55,  74,  78,  96,  79,  80,  81,
                                      75,  76,  77,  71,  72, 73, 82,  83,  86,  127, 116, 117};
    for (unsigned i = 0; i < std::size(rest); ++i) {
        map[40 + i] = rest[i];
    }
    constexpr std::uint16_t modifiers[] = {29, 42, 56, 125, 97, 54, 100, 126};
    for (int i = 0; i < 8; ++i) {
        map[224 + i] = modifiers[i];
    }
    return map;
}();
inline std::uint16_t FromEvdev(std::uint16_t code) {
    if (!code) {
        return 0;
    }
    for (std::uint16_t i = 4; i < kEvdev.size(); ++i) {
        if (kEvdev[i] == code) {
            return i;
        }
    }
    return 0;
}
}  // namespace breinput::detail
