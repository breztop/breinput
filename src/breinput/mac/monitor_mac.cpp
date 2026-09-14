#include <ApplicationServices/ApplicationServices.h>

#include <set>
#include <vector>

#include <breinput/key_mapping.hpp>
#include <breinput/monitor.hpp>
namespace breinput {
namespace {
class MacCapture final : public CaptureBackend {
    CFMachPortRef tap_ = nullptr;
    CFRunLoopSourceRef source_ = nullptr;
    std::vector<Event> events_;
    std::set<std::uint16_t> modifiers_;
    bool overflow_ = false, disabled_ = false;
    void push(Event event) {
        if (events_.size() < 256) {
            events_.push_back(std::move(event));
        } else {
            overflow_ = true;
        }
    }
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void* data) {
        auto* self = static_cast<MacCapture*>(data);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            self->disabled_ = true;
            return event;
        }
        if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
            const auto usage =
                HidFromMacKeyCode(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
            if (usage) {
                bool down = type == kCGEventKeyDown;
                if (type == kCGEventFlagsChanged) {
                    down = !self->modifiers_.contains(usage);
                    if (down) {
                        self->modifiers_.insert(usage);
                    } else {
                        self->modifiers_.erase(usage);
                    }
                }
                self->push(Key{usage, down});
            }
        } else if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
                   type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged) {
            const auto p = CGEventGetLocation(event);
            self->push(Pointer{CoordinateSpace::Desktop, int(p.x), int(p.y)});
        } else if (type == kCGEventScrollWheel) {
            self->push(
                Scroll{int(CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2)),
                       int(CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1))});
        } else {
            unsigned button =
                unsigned(CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber)) + 1;
            if (button <= 5) {
                self->push(Button{std::uint8_t(button), type == kCGEventLeftMouseDown ||
                                                            type == kCGEventRightMouseDown ||
                                                            type == kCGEventOtherMouseDown});
            }
        }
        return event;
    }

public:
    Error Start(const MonitorConfig& config, std::stop_token stop) override {
        if (config.mode != CaptureMode::Global) {
            return {ErrorCode::Unsupported, "Controlled capture is a Wayland Portal feature"};
        }
        if (stop.stop_requested()) {
            return {ErrorCode::Cancelled, "Monitor startup cancelled"};
        }
        events_.reserve(256);
        const CGEventMask mask =
            CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
            CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventMouseMoved) |
            CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged) |
            CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventLeftMouseDown) |
            CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
            CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventOtherMouseDown) |
            CGEventMaskBit(kCGEventOtherMouseUp) | CGEventMaskBit(kCGEventScrollWheel);
        tap_ = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionListenOnly, mask, callback, this);
        if (!tap_) {
            return {ErrorCode::PermissionDenied,
                    "Enable Input Monitoring permission for this application"};
        }
        source_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
        if (!source_) {
            return {ErrorCode::BackendFailure, "Cannot create input run loop source"};
        }
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source_, kCFRunLoopCommonModes);
        CGEventTapEnable(tap_, true);
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, false}; }
    Error Poll(const EventCallback& enqueue) override {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        for (const auto& event : events_) {
            enqueue(event);
        }
        events_.clear();
        if (disabled_) {
            return {ErrorCode::PermissionDenied, "System disabled the input event tap"};
        }
        if (overflow_) {
            overflow_ = false;
            return {ErrorCode::QueueFull, "Native input burst exceeded queue budget"};
        }
        return {};
    }
    void Stop() noexcept override {
        if (source_) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source_, kCFRunLoopCommonModes);
            CFRelease(source_);
            source_ = nullptr;
        }
        if (tap_) {
            CGEventTapEnable(tap_, false);
            CFMachPortInvalidate(tap_);
            CFRelease(tap_);
            tap_ = nullptr;
        }
        modifiers_.clear();
        events_.clear();
        disabled_ = false;
    }
};
}  // namespace
std::unique_ptr<CaptureBackend> CreateCaptureBackend(const MonitorConfig& config) {
    return std::make_unique<MacCapture>();
}
}  // namespace breinput
