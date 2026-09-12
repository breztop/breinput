#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>

#include <vector>

#include "key_mapping.hpp"
#include "monitor.hpp"
namespace breinput {
namespace {
class WindowsCapture final : public CaptureBackend {
    static thread_local WindowsCapture* current_;
    HHOOK keyboard_ = nullptr, mouse_ = nullptr;
    std::vector<Event> events_;
    bool overflow_ = false;
    void push(Event event) {
        if (events_.size() < 256) {
            events_.push_back(std::move(event));
        } else {
            overflow_ = true;
        }
    }
    static LRESULT CALLBACK keyHook(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && current_) {
            const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            if (!(key->flags & LLKHF_INJECTED)) {
                const auto scan =
                    std::uint16_t(key->scanCode | ((key->flags & LLKHF_EXTENDED) ? 0xe000 : 0));
                if (auto usage = HidFromWindowsScanCode(scan)) {
                    current_->push(Key{usage, message == WM_KEYDOWN || message == WM_SYSKEYDOWN});
                }
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && current_) {
            const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
            if (!(mouse->flags & LLMHF_INJECTED)) {
                if (message == WM_MOUSEMOVE) {
                    current_->push(Pointer{CoordinateSpace::Desktop, mouse->pt.x, mouse->pt.y});
                } else if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
                    int amount = short(HIWORD(mouse->mouseData)) / WHEEL_DELTA;
                    current_->push(Scroll{message == WM_MOUSEHWHEEL ? amount : 0,
                                          message == WM_MOUSEWHEEL ? amount : 0});
                } else {
                    unsigned button = 0;
                    bool down = false;
                    switch (message) {
                        case WM_LBUTTONDOWN:
                            down = true;
                            button = 1;
                            break;
                        case WM_LBUTTONUP:
                            button = 1;
                            break;
                        case WM_RBUTTONDOWN:
                            down = true;
                            button = 2;
                            break;
                        case WM_RBUTTONUP:
                            button = 2;
                            break;
                        case WM_MBUTTONDOWN:
                            down = true;
                            button = 3;
                            break;
                        case WM_MBUTTONUP:
                            button = 3;
                            break;
                        case WM_XBUTTONDOWN:
                            down = true;
                            button = HIWORD(mouse->mouseData) == XBUTTON1 ? 4 : 5;
                            break;
                        case WM_XBUTTONUP:
                            button = HIWORD(mouse->mouseData) == XBUTTON1 ? 4 : 5;
                            break;
                        default:
                            break;
                    }
                    if (button) {
                        current_->push(Button{std::uint8_t(button), down});
                    }
                }
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
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
        current_ = this;
        MSG msg{};
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
        keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyHook, GetModuleHandleW(nullptr), 0);
        mouse_ = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
        if (!keyboard_ || !mouse_) {
            return {ErrorCode::PermissionDenied, "Cannot install keyboard/mouse hooks"};
        }
        return {};
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, false}; }
    Error Poll(const EventCallback& enqueue) override {
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return {ErrorCode::Cancelled, "Input message loop closed"};
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        for (const auto& event : events_) {
            enqueue(event);
        }
        events_.clear();
        if (overflow_) {
            overflow_ = false;
            return {ErrorCode::QueueFull, "Native input burst exceeded queue budget"};
        }
        return {};
    }
    void Stop() noexcept override {
        if (keyboard_) {
            UnhookWindowsHookEx(keyboard_);
            keyboard_ = nullptr;
        }
        if (mouse_) {
            UnhookWindowsHookEx(mouse_);
            mouse_ = nullptr;
        }
        current_ = nullptr;
        events_.clear();
    }
};
thread_local WindowsCapture* WindowsCapture::current_ = nullptr;
}  // namespace
std::unique_ptr<CaptureBackend> CreateCaptureBackend(const MonitorConfig& config) {
    return std::make_unique<WindowsCapture>();
}
}  // namespace breinput
