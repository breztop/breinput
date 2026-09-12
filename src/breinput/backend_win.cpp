#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>

#include <algorithm>
#include <map>
#include <type_traits>

#include "device.hpp"
#include "key_mapping.hpp"
namespace breinput {
namespace {
class WindowsBackend final : public InjectionBackend {
    Rectangle target_;
    bool touch_ = false;
    std::map<unsigned, POINTER_TOUCH_INFO> contacts_;
    Error send(INPUT input) {
        if (SendInput(1, &input, sizeof(input)) != 1) {
            return {ErrorCode::PermissionDenied,
                    "SendInput failed (integrity level/UIPI or desktop permission)"};
        }
        return {};
    }

public:
    Error Start(const DeviceConfig& config, std::stop_token stop) override {
        if (stop.stop_requested()) {
            return {ErrorCode::Cancelled, "Input startup cancelled"};
        }
        target_ = config.target;
        if (!target_.width) {
            target_ = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                       GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        }
        touch_ = InitializeTouchInjection(10, TOUCH_FEEDBACK_NONE) != FALSE;
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, touch_}; }
    Error Inject(const Event& event) override {
        return std::visit(
            [this](const auto& v) -> Error {
                using T = std::decay_t<decltype(v)>;
                INPUT input{};
                if constexpr (std::is_same_v<T, Key>) {
                    input.type = INPUT_KEYBOARD;
                    const auto scan = WindowsScanCodeFromHid(v.usage);
                    const bool extended = (scan & 0xff00) != 0;
                    const auto code = scan & 255;
                    if (!code) {
                        return {ErrorCode::Unsupported, "HID key has no Windows scan-code mapping"};
                    }
                    input.ki.wScan = code;
                    input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) |
                                       (v.down ? 0 : KEYEVENTF_KEYUP);
                } else if constexpr (std::is_same_v<T, Pointer>) {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
                    if (v.space == CoordinateSpace::RelativeMotion) {
                        input.mi.dx = v.x;
                        input.mi.dy = v.y;
                    } else {
                        const auto x =
                            v.space == CoordinateSpace::Normalized
                                ? target_.x + std::int64_t(v.x) * (target_.width - 1) / 65535
                                : v.x;
                        const auto y =
                            v.space == CoordinateSpace::Normalized
                                ? target_.y + std::int64_t(v.y) * (target_.height - 1) / 65535
                                : v.y;
                        input.mi.dx = LONG((x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * 65535 /
                                           std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1));
                        input.mi.dy = LONG((y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * 65535 /
                                           std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1));
                        input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
                    }
                } else if constexpr (std::is_same_v<T, Button>) {
                    input.type = INPUT_MOUSE;
                    constexpr DWORD down[] = {0,
                                              MOUSEEVENTF_LEFTDOWN,
                                              MOUSEEVENTF_RIGHTDOWN,
                                              MOUSEEVENTF_MIDDLEDOWN,
                                              MOUSEEVENTF_XDOWN,
                                              MOUSEEVENTF_XDOWN};
                    constexpr DWORD up[] = {0,
                                            MOUSEEVENTF_LEFTUP,
                                            MOUSEEVENTF_RIGHTUP,
                                            MOUSEEVENTF_MIDDLEUP,
                                            MOUSEEVENTF_XUP,
                                            MOUSEEVENTF_XUP};
                    input.mi.dwFlags = v.down ? down[v.button] : up[v.button];
                    if (v.button >= 4) {
                        input.mi.mouseData = v.button == 4 ? XBUTTON1 : XBUTTON2;
                    }
                } else if constexpr (std::is_same_v<T, Scroll>) {
                    input.type = INPUT_MOUSE;
                    Error error;
                    if (v.x) {
                        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
                        input.mi.mouseData = DWORD(v.x * WHEEL_DELTA);
                        error = send(input);
                    }
                    if (!error && v.y) {
                        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                        input.mi.mouseData = DWORD(v.y * WHEEL_DELTA);
                        error = send(input);
                    }
                    return error;
                } else {
                    if (!touch_) {
                        return {ErrorCode::Unsupported, "Windows touch injection unavailable"};
                    }
                    if (v.phase != TouchPhase::Down && !contacts_.contains(v.id)) {
                        return {};
                    }
                    const auto previous = contacts_;
                    if (v.phase == TouchPhase::Down && contacts_.contains(v.id)) {
                        return {ErrorCode::InvalidArgument, "Touch contact already exists"};
                    }
                    auto& contact = contacts_[v.id];
                    contact = {};
                    contact.pointerInfo.pointerType = PT_TOUCH;
                    contact.pointerInfo.pointerId = v.id + 1;
                    contact.pointerInfo.ptPixelLocation = {
                        LONG(target_.x + std::int64_t(v.x) * (target_.width - 1) / 65535),
                        LONG(target_.y + std::int64_t(v.y) * (target_.height - 1) / 65535)};
                    if (v.phase == TouchPhase::Up || v.phase == TouchPhase::Cancel) {
                        contact.pointerInfo.ptPixelLocation =
                            previous.at(v.id).pointerInfo.ptPixelLocation;
                    }
                    contact.pointerInfo.pointerFlags =
                        v.phase == TouchPhase::Down
                            ? POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT
                        : v.phase == TouchPhase::Move
                            ? POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT
                        : v.phase == TouchPhase::Cancel ? POINTER_FLAG_UP | POINTER_FLAG_CANCELED
                                                        : POINTER_FLAG_UP;
                    POINTER_TOUCH_INFO batch[10]{};
                    unsigned count = 0;
                    for (auto& [id, c] : contacts_) {
                        batch[count++] = c;
                    }
                    const bool ok = InjectTouchInput(count, batch) != FALSE;
                    const auto last_error = ok ? 0 : GetLastError();
                    if (!ok) {
                        contacts_ = previous;
                        return {ErrorCode::BackendFailure,
                                "InjectTouchInput failed: " + std::to_string(last_error)};
                    }
                    if (v.phase == TouchPhase::Up || v.phase == TouchPhase::Cancel) {
                        contacts_.erase(v.id);
                    }
                    for (auto& [id, c] : contacts_) {
                        c.pointerInfo.pointerFlags =
                            POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
                    }
                    return {};
                }
                return send(input);
            },
            event);
    }
    void Stop() noexcept override { contacts_.clear(); }
};
}  // namespace
std::unique_ptr<InjectionBackend> CreateInjectionBackend() {
    return std::make_unique<WindowsBackend>();
}
}  // namespace breinput
