#pragma once

#include <cstdint>
#include <variant>

namespace breinput {
// 键盘标识使用 USB HID Keyboard/Keypad Usage Page (0x07)，与当前键盘布局无关。
struct Key {
    std::uint16_t usage = 0;
    bool down = false;
    bool operator==(const Key&) const = default;
};
enum class CoordinateSpace : std::uint8_t { Desktop, RelativeMotion, Normalized };
// Normalized 的坐标范围为 [0, 65535]；Desktop 为有符号桌面像素。
struct Pointer {
    CoordinateSpace space = CoordinateSpace::Desktop;
    std::int32_t x = 0, y = 0;
    bool operator==(const Pointer&) const = default;
};
// USB HID 按钮编号：1 左、2 右、3 中、4/5 侧键。
struct Button {
    std::uint8_t button = 1;
    bool down = false;
    bool operator==(const Button&) const = default;
};
// 单位为一个滚轮刻度；x 正向右，y 正向上。
struct Scroll {
    std::int32_t x = 0, y = 0;
    bool operator==(const Scroll&) const = default;
};
enum class TouchPhase : std::uint8_t { Down, Move, Up, Cancel };
struct Touch {
    TouchPhase phase = TouchPhase::Down;
    std::uint8_t id = 0;
    std::int32_t x = 0, y = 0;
    bool operator==(const Touch&) const = default;
};
// Touch 始终使用 Normalized 坐标，最多十个接触点。释放所有按键由会话 API 表达。
using Event = std::variant<Key, Pointer, Button, Scroll, Touch>;
struct Rectangle {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
};
bool IsValid(const Event& event) noexcept;
// 将预览窗坐标映射到画面。黑边返回 false；拖动释放时可选择 clamp。
bool MapPoint(Rectangle viewport, int video_width, int video_height, int rotation, std::int32_t x,
              std::int32_t y, Pointer& result, bool clamp = false) noexcept;
}  // namespace breinput
