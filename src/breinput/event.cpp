#include "event.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "device.hpp"

namespace breinput {
bool IsValid(const Event& event) noexcept {
    return std::visit(
        [](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Key>) {
                return v.usage >= 4 && v.usage <= 231;
            } else if constexpr (std::is_same_v<T, Pointer>) {
                if (v.space > CoordinateSpace::Normalized) {
                    return false;
                }
                if (v.space == CoordinateSpace::Normalized) {
                    return v.x >= 0 && v.y >= 0 && v.x <= 65535 && v.y <= 65535;
                }
                return v.x >= -1'000'000 && v.x <= 1'000'000 && v.y >= -1'000'000 &&
                       v.y <= 1'000'000;
            } else if constexpr (std::is_same_v<T, Button>) {
                return v.button >= 1 && v.button <= 5;
            } else if constexpr (std::is_same_v<T, Scroll>) {
                return v.x >= -120 && v.x <= 120 && v.y >= -120 && v.y <= 120;
            } else {
                return v.phase <= TouchPhase::Cancel && v.id < 10 && v.x >= 0 && v.y >= 0 &&
                       v.x <= 65535 && v.y <= 65535;
            }
        },
        event);
}
bool Capabilities::Supports(const Event& event) const noexcept {
    return std::visit(
        [this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Key>) {
                return keyboard;
            } else if constexpr (std::is_same_v<T, Pointer>) {
                return pointer;
            } else if constexpr (std::is_same_v<T, Button>) {
                return buttons;
            } else if constexpr (std::is_same_v<T, Scroll>) {
                return scroll;
            } else {
                return touch;
            }
        },
        event);
}
bool MapPoint(Rectangle viewport, int width, int height, int rotation, int x, int y,
              Pointer& result, bool clamp) noexcept {
    if (width <= 0 || height <= 0 || viewport.width <= 0 || viewport.height <= 0 ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
        return false;
    }
    if (rotation == 90 || rotation == 270) {
        std::swap(width, height);
    }
    const double scale = std::min(double(viewport.width) / width, double(viewport.height) / height);
    double px = (double(x) - viewport.x - (viewport.width - width * scale) / 2) / (width * scale);
    double py =
        (double(y) - viewport.y - (viewport.height - height * scale) / 2) / (height * scale);
    if (!clamp && (px < 0 || px >= 1 || py < 0 || py >= 1)) {
        return false;
    }
    px = std::clamp(px, 0.0, 1.0);
    py = std::clamp(py, 0.0, 1.0);
    if (rotation == 90) {
        const auto old = px;
        px = py;
        py = 1 - old;
    } else if (rotation == 180) {
        px = 1 - px;
        py = 1 - py;
    } else if (rotation == 270) {
        const auto old = px;
        px = 1 - py;
        py = old;
    }
    result = {CoordinateSpace::Normalized, int(std::lround(px * 65535)),
              int(std::lround(py * 65535))};
    return true;
}
}  // namespace breinput
