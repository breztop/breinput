#include <X11/Xlib.h>
#include <X11/extensions/record.h>
#include <poll.h>

#include <array>
#include <cstdlib>
#include <cstring>

#include "breinput/key_map.hpp"
#include <breinput/monitor.hpp>
namespace breinput {
std::unique_ptr<CaptureBackend> CreatePortalCaptureBackend();
namespace {
class X11Capture final : public CaptureBackend {
    Display* control_ = nullptr;
    Display* data_ = nullptr;
    XRecordContext context_ = 0;
    std::array<Event, 256> events_;
    std::size_t count_ = 0;
    bool overflow_ = false;
    std::stop_token stop_;
    void push(Event event) {
        if (count_ < events_.size()) {
            events_[count_++] = std::move(event);
        } else {
            overflow_ = true;
        }
    }
    static void callback(XPointer closure, XRecordInterceptData* record) {
        auto* self = reinterpret_cast<X11Capture*>(closure);
        if (record->category == XRecordFromServer && !record->client_swapped) {
            for (unsigned long offset = 0; offset + 32 <= record->data_len * 4; offset += 32) {
                const auto* bytes = record->data + offset;
                const auto type = bytes[0] & 127;
                const auto detail = bytes[1];
                if (type == KeyPress || type == KeyRelease) {
                    if (auto usage = breinput::detail::FromEvdev(detail >= 8 ? detail - 8 : 0)) {
                        self->push(Key{usage, type == KeyPress});
                    }
                } else if (type == MotionNotify) {
                    std::int16_t x = 0, y = 0;
                    std::memcpy(&x, bytes + 16, 2);
                    std::memcpy(&y, bytes + 18, 2);
                    self->push(Pointer{CoordinateSpace::Desktop, x, y});
                } else if (type == ButtonPress || type == ButtonRelease) {
                    if (detail >= 4 && detail <= 7) {
                        if (type == ButtonPress) {
                            self->push(Scroll{detail == 6   ? -1
                                              : detail == 7 ? 1
                                                            : 0,
                                              detail == 4   ? 1
                                              : detail == 5 ? -1
                                                            : 0});
                        }
                    } else {
                        const unsigned button = detail == 1   ? 1
                                                : detail == 2 ? 3
                                                : detail == 3 ? 2
                                                : detail == 8 ? 4
                                                : detail == 9 ? 5
                                                              : 0;
                        if (button) {
                            self->push(Button{std::uint8_t(button), type == ButtonPress});
                        }
                    }
                }
            }
        }
        XRecordFreeData(record);
    }

public:
    Error Start(const MonitorConfig&, std::stop_token stop) override {
        stop_ = stop;
        XInitThreads();
        if (std::getenv("WAYLAND_DISPLAY") ||
            (std::getenv("XDG_SESSION_TYPE") &&
             std::strcmp(std::getenv("XDG_SESSION_TYPE"), "wayland") == 0)) {
            return {ErrorCode::Unsupported,
                    "Wayland global monitoring requires InputCapture Portal activation; use window "
                    "input callbacks"};
        }
        control_ = XOpenDisplay(nullptr);
        data_ = XOpenDisplay(nullptr);
        if (!control_ || !data_) {
            return {ErrorCode::Unavailable, "Cannot open X11 display for XRecord"};
        }
        int major = 0, minor = 0;
        if (!XRecordQueryVersion(control_, &major, &minor)) {
            return {ErrorCode::Unsupported, "XRecord unavailable"};
        }
        auto* range = XRecordAllocRange();
        if (!range) {
            return {ErrorCode::Unavailable, "Cannot allocate XRecord range"};
        }
        range->device_events.first = KeyPress;
        range->device_events.last = MotionNotify;
        XRecordClientSpec client = XRecordAllClients;
        context_ = XRecordCreateContext(control_, 0, &client, 1, &range, 1);
        XFree(range);
        if (!context_) {
            return {ErrorCode::BackendFailure, "XRecordCreateContext failed"};
        }
        XSync(control_, False);
        if (!XRecordEnableContextAsync(data_, context_, callback,
                                       reinterpret_cast<XPointer>(this))) {
            return {ErrorCode::BackendFailure, "XRecordEnableContextAsync failed"};
        }
        XFlush(data_);
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, false}; }
    Error Poll(const EventCallback& enqueue) override {
        pollfd fd{ConnectionNumber(data_), POLLIN, 0};
        const int result = poll(&fd, 1, 10);
        if (result > 0 && (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            return {ErrorCode::Unavailable, "XRecord connection closed"};
        }
        if (result > 0) {
            XRecordProcessReplies(data_);
        }
        for (std::size_t i = 0; i < count_; ++i) {
            if (stop_.stop_requested()) {
                break;
            }
            enqueue(events_[i]);
        }
        count_ = 0;
        if (overflow_) {
            overflow_ = false;
            return {ErrorCode::QueueFull, "XRecord event burst exceeded input queue budget"};
        }
        return {};
    }
    void Stop() noexcept override {
        if (control_ && context_) {
            XRecordDisableContext(control_, context_);
            XSync(control_, False);
            XRecordFreeContext(control_, context_);
            context_ = 0;
        }
        if (data_) {
            XCloseDisplay(data_);
            data_ = nullptr;
        }
        if (control_) {
            XCloseDisplay(control_);
            control_ = nullptr;
        }
    }
};
}  // namespace
std::unique_ptr<CaptureBackend> CreateCaptureBackend(const MonitorConfig& config) {
    if (config.mode == CaptureMode::Controlled) {
        return CreatePortalCaptureBackend();
    }
    return std::make_unique<X11Capture>();
}
}  // namespace breinput
