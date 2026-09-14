#include "eis_sender.hpp"

#include <poll.h>

#include <chrono>
#include <type_traits>

#include "breinput/key_map.hpp"
namespace breinput::detail {
EisSender::~EisSender() {
    for (auto& [id, touch] : touches_) {
        api_->ei_touch_unref(touch);
    }
    for (auto& [device, resumed] : devices_) {
        if (resumed && !disconnected_) {
            api_->ei_device_stop_emulating(device);
        }
        api_->ei_device_unref(device);
    }
    if (context_) {
        api_->ei_unref(context_);
    }
}
Error EisSender::Start(int fd, std::string mapping_id, std::stop_token stop) {
    mapping_id_ = std::move(mapping_id);
    context_ = api_->ei_new_sender(nullptr);
    api_->ei_configure_name(context_, "BreInput");
    if (api_->ei_setup_backend_fd(context_, fd) < 0) {
        return {ErrorCode::BackendFailure, "Cannot attach EIS socket"};
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (stop.stop_requested()) {
            return {ErrorCode::Cancelled, "EIS startup cancelled"};
        }
        if (auto error = Poll()) {
            return error;
        }
        const auto caps = GetCapabilities();
        if (caps.keyboard || caps.pointer || caps.touch) {
            return {};
        }
        pollfd descriptor{api_->ei_get_fd(context_), POLLIN, 0};
        poll(&descriptor, 1, 10);
    }
    return {ErrorCode::Unavailable, "EIS provided no usable input devices"};
}
Error EisSender::Poll() {
    if (disconnected_) {
        return {};
    }
    api_->ei_dispatch(context_);
    while (auto* event = api_->ei_get_event(context_)) {
        const auto type = api_->ei_event_get_type(event);
        auto* current = api_->ei_event_get_device(event);
        if (type == EI_EVENT_SEAT_ADDED) {
            api_->ei_seat_bind_capabilities(api_->ei_event_get_seat(event), EI_DEVICE_CAP_POINTER,
                                            EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_KEYBOARD,
                                            EI_DEVICE_CAP_TOUCH, EI_DEVICE_CAP_SCROLL,
                                            EI_DEVICE_CAP_BUTTON, nullptr);
        } else if (type == EI_EVENT_DEVICE_ADDED) {
            devices_.emplace(api_->ei_device_ref(current), false);
        } else if (type == EI_EVENT_DEVICE_RESUMED) {
            devices_[current] = true;
            api_->ei_device_start_emulating(current, ++sequence_);
        } else if (type == EI_EVENT_DEVICE_PAUSED) {
            devices_[current] = false;
        } else if (type == EI_EVENT_DEVICE_REMOVED) {
            for (auto it = touches_.begin(); it != touches_.end();) {
                if (api_->ei_touch_get_device(it->second) == current) {
                    api_->ei_touch_unref(it->second);
                    it = touches_.erase(it);
                } else {
                    ++it;
                }
            }
            auto it = devices_.find(current);
            if (it != devices_.end()) {
                api_->ei_device_unref(it->first);
                devices_.erase(it);
            }
        } else if (type == EI_EVENT_DISCONNECT) {
            disconnected_ = true;
        }
        api_->ei_event_unref(event);
    }
    return disconnected_ ? Error{ErrorCode::Unavailable, "Portal EIS connection closed"} : Error{};
}
ei_region* EisSender::region(ei_device* device) const {
    for (std::size_t i = 0; auto* region = api_->ei_device_get_region(device, i); ++i) {
        const auto* id = api_->ei_region_get_mapping_id(region);
        if (!mapping_id_.empty() && id && mapping_id_ == id) {
            return region;
        }
    }
    // 无映射标识时不猜测多屏归属；只接受唯一绝对设备的唯一区域。
    if (!mapping_id_.empty() || api_->ei_device_get_region(device, 1)) {
        return nullptr;
    }
    unsigned count = 0;
    for (const auto& [candidate, ready] : devices_) {
        if (ready && api_->ei_device_get_region(candidate, 0)) {
            ++count;
        }
    }
    return count == 1 ? api_->ei_device_get_region(device, 0) : nullptr;
}
ei_device* EisSender::device(ei_device_capability cap) const {
    for (auto& [candidate, ready] : devices_) {
        if (ready && api_->ei_device_has_capability(candidate, cap)) {
            if ((cap == EI_DEVICE_CAP_POINTER_ABSOLUTE || cap == EI_DEVICE_CAP_TOUCH) &&
                !region(candidate)) {
                continue;
            }
            return candidate;
        }
    }
    return nullptr;
}
Capabilities EisSender::GetCapabilities() const {
    if (disconnected_) {
        return {};
    }
    return {device(EI_DEVICE_CAP_KEYBOARD) != nullptr,
            device(EI_DEVICE_CAP_POINTER) != nullptr ||
                device(EI_DEVICE_CAP_POINTER_ABSOLUTE) != nullptr,
            device(EI_DEVICE_CAP_BUTTON) != nullptr, device(EI_DEVICE_CAP_SCROLL) != nullptr,
            device(EI_DEVICE_CAP_TOUCH) != nullptr};
}
Error EisSender::Inject(const Event& event) {
    if (auto error = Poll()) {
        return error;
    }
    if (disconnected_) {
        return {ErrorCode::NotRunning, "EIS is disconnected"};
    }
    return std::visit(
        [this](const auto& v) -> Error {
            using T = std::decay_t<decltype(v)>;
            ei_device_capability cap;
            if constexpr (std::is_same_v<T, Key>) {
                cap = EI_DEVICE_CAP_KEYBOARD;
            } else if constexpr (std::is_same_v<T, Pointer>) {
                cap = v.space == CoordinateSpace::RelativeMotion ? EI_DEVICE_CAP_POINTER
                                                                 : EI_DEVICE_CAP_POINTER_ABSOLUTE;
            } else if constexpr (std::is_same_v<T, Button>) {
                cap = EI_DEVICE_CAP_BUTTON;
            } else if constexpr (std::is_same_v<T, Scroll>) {
                cap = EI_DEVICE_CAP_SCROLL;
            } else {
                cap = EI_DEVICE_CAP_TOUCH;
            }
            auto* target = device(cap);
            if (!target) {
                return {ErrorCode::Unavailable,
                        "EIS device paused, removed or screen mapping unavailable"};
            }
            if constexpr (std::is_same_v<T, Key>) {
                const auto code = kEvdev[v.usage];
                if (!code) {
                    return {ErrorCode::Unsupported, "Unknown HID key mapping"};
                }
                api_->ei_device_keyboard_key(target, code, v.down);
            } else if constexpr (std::is_same_v<T, Button>) {
                constexpr unsigned buttons[] = {0, 0x110, 0x111, 0x112, 0x113, 0x114};
                api_->ei_device_button_button(target, buttons[v.button], v.down);
            } else if constexpr (std::is_same_v<T, Scroll>) {
                api_->ei_device_scroll_discrete(target, v.x * 120, -v.y * 120);
            } else {
                double x = v.x, y = v.y;
                if constexpr (std::is_same_v<T, Pointer>) {
                    if (v.space == CoordinateSpace::RelativeMotion) {
                        api_->ei_device_pointer_motion(target, x, y);
                        api_->ei_device_frame(target, api_->ei_now(context_));
                        return {};
                    }
                    if (v.space != CoordinateSpace::Normalized) {
                        return {ErrorCode::Unsupported,
                                "EIS absolute input requires normalized picture coordinates"};
                    }
                }
                auto* area = region(target);
                if (!area) {
                    return {ErrorCode::Unavailable, "EIS picture mapping unavailable"};
                }
                x = api_->ei_region_get_x(area) + x * (api_->ei_region_get_width(area) - 1) / 65535;
                y = api_->ei_region_get_y(area) +
                    y * (api_->ei_region_get_height(area) - 1) / 65535;
                if constexpr (std::is_same_v<T, Pointer>) {
                    api_->ei_device_pointer_motion_absolute(target, x, y);
                } else {
                    auto it = touches_.find(v.id);
                    if (v.phase == TouchPhase::Down) {
                        if (it != touches_.end()) {
                            return {ErrorCode::InvalidArgument, "Touch contact already exists"};
                        }
                        auto* touch = api_->ei_device_touch_new(target);
                        touches_[v.id] = touch;
                        api_->ei_touch_down(touch, x, y);
                    } else if (it != touches_.end()) {
                        if (api_->ei_touch_get_device(it->second) != target) {
                            return {ErrorCode::Unavailable, "Touch device changed"};
                        }
                        if (v.phase == TouchPhase::Move) {
                            api_->ei_touch_motion(it->second, x, y);
                        } else {
                            api_->ei_touch_up(it->second);
                            api_->ei_touch_unref(it->second);
                            touches_.erase(it);
                        }
                    }
                }
            }
            api_->ei_device_frame(target, api_->ei_now(context_));
            return {};
        },
        event);
}
}  // namespace breinput::detail
