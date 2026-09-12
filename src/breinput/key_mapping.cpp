#include "key_mapping.hpp"

#include "key_map.hpp"
namespace breinput {
std::uint16_t WindowsScanCodeFromHid(std::uint16_t usage) {
    if (usage >= detail::kEvdev.size()) {
        return 0;
    }
    auto code = detail::kEvdev[usage];
    bool extended = false;
    switch (code) {
        case 96:
            code = 28;
            extended = true;
            break;
        case 97:
            code = 29;
            extended = true;
            break;
        case 98:
            code = 53;
            extended = true;
            break;
        case 99:
            code = 55;
            extended = true;
            break;
        case 100:
            code = 56;
            extended = true;
            break;
        case 102:
            code = 71;
            extended = true;
            break;
        case 103:
            code = 72;
            extended = true;
            break;
        case 104:
            code = 73;
            extended = true;
            break;
        case 105:
            code = 75;
            extended = true;
            break;
        case 106:
            code = 77;
            extended = true;
            break;
        case 107:
            code = 79;
            extended = true;
            break;
        case 108:
            code = 80;
            extended = true;
            break;
        case 109:
            code = 81;
            extended = true;
            break;
        case 110:
            code = 82;
            extended = true;
            break;
        case 111:
            code = 83;
            extended = true;
            break;
        case 125:
            code = 91;
            extended = true;
            break;
        case 126:
            code = 92;
            extended = true;
            break;
        case 127:
            code = 93;
            extended = true;
            break;
        default:
            if (code > 88) {
                code = 0;
            }
            break;
    }

    return extended ? (code | 0xe000) : code;
}
std::uint16_t HidFromWindowsScanCode(std::uint16_t scan) {
    if (!scan) {
        return 0;
    }
    for (std::uint16_t usage = 4; usage < 232; ++usage) {
        if (WindowsScanCodeFromHid(usage) == scan) {
            return usage;
        }
    }
    return 0;
}
int MacKeyCodeFromHid(std::uint16_t usage) {
    static constexpr auto map = [] {
        std::array<int, 232> m{};
        m.fill(-1);
        constexpr int letters[] = {0,  11, 8,  2,  14, 3, 5,  4,  34, 38, 40, 37, 46,
                                   45, 31, 35, 12, 15, 1, 17, 32, 9,  13, 7,  16, 6};
        for (int i = 0; i < 26; ++i) {
            m[4 + i] = letters[i];
        }
        constexpr int rest[] = {18,  19,  20,  21,  23,  22,  26,  28,  25,  29,  36,  53,
                                51,  48,  49,  27,  24,  33,  30,  42,  -1,  41,  39,  50,
                                43,  47,  44,  57,  122, 120, 99,  118, 96,  97,  98,  100,
                                101, 109, 103, 111, 105, 107, 113, 114, 115, 116, 117, 119,
                                121, 124, 123, 125, 126, 71,  75,  67,  78,  69,  76,  83,
                                84,  85,  86,  87,  88,  89,  91,  92,  82,  65};
        for (unsigned i = 0; i < std::size(rest); ++i) {
            m[30 + i] = rest[i];
        }
        constexpr int mods[] = {59, 56, 58, 55, 62, 60, 61, 54};
        for (int i = 0; i < 8; ++i) {
            m[224 + i] = mods[i];
        }
        return m;
    }();

    return usage < map.size() ? map[usage] : -1;
}
std::uint16_t HidFromMacKeyCode(std::uint16_t code) {
    for (std::uint16_t usage = 4; usage < 232; ++usage) {
        if (MacKeyCodeFromHid(usage) == code) {
            return usage;
        }
    }
    return 0;
}
}  // namespace breinput
