#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <cstdlib>
#include <mutex>
#include <type_traits>

#include "device.hpp"
#include "key_map.hpp"

namespace breinput {
std::unique_ptr<InjectionBackend> CreatePortalBackend();
namespace {
class X11Backend final : public InjectionBackend {
    Display* display_ = nullptr;
    Rectangle target_;

public:
    Error Start(const DeviceConfig& config, std::stop_token stop) override {
        static std::once_flag initialized;
        std::call_once(initialized, [] {
            XInitThreads();
        });
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            return {ErrorCode::Unavailable, "Cannot open X11 display"};
        }
        int event, error, major, minor;
        if (!XTestQueryExtension(display_, &event, &error, &major, &minor)) {
            return {ErrorCode::Unsupported, "XTest extension unavailable"};
        }
        if (stop.stop_requested()) {
            return {ErrorCode::Cancelled, "Input startup cancelled"};
        }
        target_ = config.target;
        if (!target_.width) {
            target_ = {0, 0, DisplayWidth(display_, DefaultScreen(display_)),
                       DisplayHeight(display_, DefaultScreen(display_))};
        }
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, false}; }
    Error Inject(const Event& event) override {
        return std::visit(
            [this](const auto& v) -> Error {
                using T = std::decay_t<decltype(v)>;
                int ok = 1;
                if constexpr (std::is_same_v<T, Key>) {
                    const auto code = detail::kEvdev[v.usage];
                    if (!code) {
                        return {ErrorCode::Unsupported, "HID key has no evdev mapping"};
                    }
                    ok = XTestFakeKeyEvent(display_, code + 8, v.down, CurrentTime);
                } else if constexpr (std::is_same_v<T, Pointer>) {
                    if (v.space == CoordinateSpace::RelativeMotion) {
                        ok = XTestFakeRelativeMotionEvent(display_, v.x, v.y, CurrentTime);
                    } else {
                        const int x =
                            v.space == CoordinateSpace::Normalized
                                ? target_.x + std::int64_t(v.x) * (target_.width - 1) / 65535
                                : v.x;
                        const int y =
                            v.space == CoordinateSpace::Normalized
                                ? target_.y + std::int64_t(v.y) * (target_.height - 1) / 65535
                                : v.y;
                        ok = XTestFakeMotionEvent(display_, -1, x, y, CurrentTime);
                    }
                } else if constexpr (std::is_same_v<T, Button>) {
                    constexpr unsigned buttons[] = {0, 1, 3, 2, 8, 9};
                    ok = XTestFakeButtonEvent(display_, buttons[v.button], v.down, CurrentTime);
                } else if constexpr (std::is_same_v<T, Scroll>) {
                    for (int axis = 0; axis < 2; ++axis) {
                        const int amount = axis ? v.y : v.x;
                        const unsigned button = axis ? (amount > 0 ? 4 : 5) : (amount > 0 ? 7 : 6);
                        for (int i = 0; i < std::abs(amount); ++i) {
                            ok &= XTestFakeButtonEvent(display_, button, True, CurrentTime);
                            ok &= XTestFakeButtonEvent(display_, button, False, CurrentTime);
                        }
                    }
                } else {
                    return {ErrorCode::Unsupported, "X11 touch injection is unavailable"};
                }
                XFlush(display_);
                return ok ? Error{} : Error{ErrorCode::BackendFailure, "XTest rejected input"};
            },
            event);
    }
    void Stop() noexcept override {
        if (display_) {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }
};
}  // namespace
std::unique_ptr<InjectionBackend> CreateInjectionBackend() {
    // 不在 Wayland 会话中偷偷降级为只能控制 XWayland 应用的 XTest。
    const auto* session = std::getenv("XDG_SESSION_TYPE");
    if ((session && std::string(session) == "wayland") || std::getenv("WAYLAND_DISPLAY")) {
        return CreatePortalBackend();
    }
    return std::make_unique<X11Backend>();
}
}  // namespace breinput
