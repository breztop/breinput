#include <ApplicationServices/ApplicationServices.h>

#include <array>
#include <type_traits>

#include "device.hpp"
#include "key_mapping.hpp"
namespace breinput {
namespace {
class MacBackend final : public InjectionBackend {
    Rectangle target_;
    CGEventFlags flags_ = 0;
    unsigned buttons_ = 0;
    unsigned modifiers_ = 0;

public:
    Error Start(const DeviceConfig& config, std::stop_token stop) override {
        if (!AXIsProcessTrusted()) {
            return {ErrorCode::PermissionDenied,
                    "Enable Accessibility permission for this application"};
        }
        if (stop.stop_requested()) {
            return {ErrorCode::Cancelled, "Input startup cancelled"};
        }
        target_ = config.target;
        if (!target_.width) {
            const auto rect = CGDisplayBounds(CGMainDisplayID());
            target_ = {int(rect.origin.x), int(rect.origin.y), int(rect.size.width),
                       int(rect.size.height)};
        }
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, false}; }
    Error Inject(const Event& event) override {
        CGEventRef result = nullptr;
        auto error = std::visit(
            [&](const auto& v) -> Error {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Key>) {
                    if (MacKeyCodeFromHid(v.usage) < 0) {
                        return {ErrorCode::Unsupported, "HID key has no macOS mapping"};
                    }
                    if (v.usage >= 224) {
                        constexpr CGEventFlags masks[] = {
                            kCGEventFlagMaskControl, kCGEventFlagMaskShift,
                            kCGEventFlagMaskAlternate, kCGEventFlagMaskCommand};
                        const unsigned bit = 1U << (v.usage - 224);
                        if (v.down) {
                            modifiers_ |= bit;
                        } else {
                            modifiers_ &= ~bit;
                        }
                        flags_ = 0;
                        for (unsigned index = 0; index < 8; ++index) {
                            if (modifiers_ & (1U << index)) {
                                flags_ |= masks[index % 4];
                            }
                        }
                    }
                    result = CGEventCreateKeyboardEvent(
                        nullptr, CGKeyCode(MacKeyCodeFromHid(v.usage)), v.down);
                } else if constexpr (std::is_same_v<T, Pointer> || std::is_same_v<T, Button>) {
                    auto* current = CGEventCreate(nullptr);
                    if (!current) {
                        return {ErrorCode::BackendFailure, "CGEventCreate failed"};
                    }
                    auto point = CGEventGetLocation(current);
                    CFRelease(current);
                    CGEventType type = kCGEventMouseMoved;
                    CGMouseButton button = kCGMouseButtonLeft;
                    if constexpr (std::is_same_v<T, Pointer>) {
                        if (v.space == CoordinateSpace::RelativeMotion) {
                            point.x += v.x;
                            point.y += v.y;
                        } else if (v.space == CoordinateSpace::Normalized) {
                            point = {
                                double(target_.x) + double(v.x) * (target_.width - 1) / 65535,
                                double(target_.y) + double(v.y) * (target_.height - 1) / 65535};
                        } else {
                            point = {double(v.x), double(v.y)};
                        }
                        if (buttons_ & 1) {
                            type = kCGEventLeftMouseDragged;
                        } else if (buttons_ & 2) {
                            type = kCGEventRightMouseDragged;
                            button = kCGMouseButtonRight;
                        }
                    } else {
                        button = CGMouseButton(v.button - 1);
                        if (v.down) {
                            buttons_ |= 1U << (v.button - 1);
                        } else {
                            buttons_ &= ~(1U << (v.button - 1));
                        }
                        type = v.button == 1
                                   ? (v.down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
                               : v.button == 2
                                   ? (v.down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                                   : (v.down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp);
                    }
                    result = CGEventCreateMouseEvent(nullptr, type, point, button);
                } else if constexpr (std::is_same_v<T, Scroll>) {
                    result =
                        CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, v.y, v.x);
                } else {
                    return {ErrorCode::Unsupported, "macOS touch injection is unavailable"};
                }
                return {};
            },
            event);
        if (error) {
            return error;
        }
        if (!result) {
            return {ErrorCode::BackendFailure, "CoreGraphics could not create input event"};
        }
        CGEventSetFlags(result, flags_);
        CGEventPost(kCGHIDEventTap, result);
        CFRelease(result);
        return {};
    }
    void Stop() noexcept override {
        flags_ = 0;
        buttons_ = 0;
        modifiers_ = 0;
    }
};
}  // namespace
std::unique_ptr<InjectionBackend> CreateInjectionBackend() {
    return std::make_unique<MacBackend>();
}
}  // namespace breinput
