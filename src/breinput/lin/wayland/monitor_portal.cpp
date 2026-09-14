#include <poll.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ei_library.hpp"
#include "breinput/key_map.hpp"
#include <breinput/monitor.hpp>
#include "portal_connection.hpp"
namespace breinput {
namespace {
constexpr auto kCapture = "org.freedesktop.portal.InputCapture";
using detail::kBus;
using detail::kPath;
class PortalCapture final : public CaptureBackend, private detail::PortalConnection {
    detail::EiLibrary::Shared api_;
    ei* input_ = nullptr;
    guint signals_ = 0, closed_signal_ = 0;
    Capabilities capabilities_;
    bool zones_changed_ = false, disabled_ = false, deactivated_ = false;
    std::set<std::uint16_t> keys_;
    std::set<std::uint8_t> buttons_;
    std::map<std::uint32_t, Touch> touches_;
    Error barriers() {
        GVariant* values = nullptr;
        auto error = request(kCapture, "GetZones",
                             g_variant_new("(o@a{sv})", session_.c_str(), options()), &values);
        if (error) {
            return error;
        }
        guint zone_set = 0;
        g_variant_lookup(values, "zone_set", "u", &zone_set);
        auto* zones = g_variant_lookup_value(values, "zones", G_VARIANT_TYPE("a(uuii)"));
        Rectangle right;
        bool found = false;
        if (zones) {
            for (gsize i = 0; i < g_variant_n_children(zones); ++i) {
                guint width = 0, height = 0;
                gint x = 0, y = 0;
                g_variant_get_child(zones, i, "(uuii)", &width, &height, &x, &y);
                if (width && height && width <= 1'000'000 && height <= 1'000'000 &&
                    x >= -1'000'000 && x <= 1'000'000 && y >= -1'000'000 && y <= 1'000'000 &&
                    (!found || std::int64_t(x) + width > std::int64_t(right.x) + right.width)) {
                    right = {x, y, int(width), int(height)};
                    found = true;
                }
            }
            g_variant_unref(zones);
        }
        g_variant_unref(values);
        if (!found) {
            return {ErrorCode::Unavailable, "InputCapture provided no screen zones"};
        }
        GVariantBuilder barrier;
        g_variant_builder_init(&barrier, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&barrier, "{sv}", "barrier_id", g_variant_new_uint32(1));
        // 右边界在最后一个像素的右侧，见 InputCapture SetPointerBarriers 坐标定义。
        const int edge = right.x + right.width;
        g_variant_builder_add(
            &barrier, "{sv}", "position",
            g_variant_new("(iiii)", edge, right.y, edge, right.y + right.height - 1));
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
        g_variant_builder_add_value(&list, g_variant_builder_end(&barrier));
        error = request(kCapture, "SetPointerBarriers",
                        g_variant_new("(o@a{sv}@aa{sv}u)", session_.c_str(), options(),
                                      g_variant_builder_end(&list), zone_set),
                        &values);
        if (error) {
            return error;
        }
        auto* failures = g_variant_lookup_value(values, "failed_barriers", G_VARIANT_TYPE("au"));
        const bool failed = failures && g_variant_n_children(failures) > 0;
        if (failures) {
            g_variant_unref(failures);
        }
        g_variant_unref(values);
        return failed ? Error{ErrorCode::Unsupported, "Compositor rejected the capture edge"}
                      : Error{};
    }
    void release(const EventCallback& enqueue) {
        for (auto key : keys_) {
            enqueue(Key{key, false});
        }
        keys_.clear();
        for (auto button : buttons_) {
            enqueue(Button{button, false});
        }
        buttons_.clear();
        for (auto& [id, touch] : touches_) {
            touch.phase = TouchPhase::Cancel;
            enqueue(touch);
        }
        touches_.clear();
    }

public:
    Error Start(const MonitorConfig& config, std::stop_token stop) override {
        Error load_error;
        api_ = detail::EiLibrary::Load(load_error);
        if (!api_) {
            return load_error;
        }
        if (auto error = open(stop)) {
            return error;
        }
        GError* native_error = nullptr;
        guint version = 1;
        auto* property = g_dbus_connection_call_sync(
            bus_, kBus, kPath, "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", kCapture, "version"), G_VARIANT_TYPE("(v)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
        if (property) {
            GVariant* value = nullptr;
            g_variant_get(property, "(v)", &value);
            version = g_variant_get_uint32(value);
            g_variant_unref(value);
            g_variant_unref(property);
        }
        GVariant* values = nullptr;
        Error error;
        GVariantBuilder options_builder;
        g_variant_builder_init(&options_builder, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options_builder, "{sv}", "capabilities", g_variant_new_uint32(7));
        if (version >= 2) {
            auto* response = g_dbus_connection_call_sync(
                bus_, kBus, kPath, kCapture, "CreateSession2", g_variant_new("(@a{sv})", options()),
                G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &native_error);
            if (!response) {
                std::string message =
                    native_error ? native_error->message : "Cannot create InputCapture session";
                g_clear_error(&native_error);
                g_variant_builder_clear(&options_builder);
                return {ErrorCode::Unavailable, message};
            }
            g_variant_get(response, "(@a{sv})", &values);
            g_variant_unref(response);
        } else {
            error = request(kCapture, "CreateSession",
                            g_variant_new("(s@a{sv})", config.parent_window.c_str(),
                                          g_variant_builder_end(&options_builder)),
                            &values);
            if (error) {
                return error;
            }
        }
        const char* session = nullptr;
        g_variant_lookup(values, "session_handle", "&o", &session);
        if (session) {
            session_ = session;
        }
        if (session_.empty()) {
            g_variant_unref(values);
            return {ErrorCode::BackendFailure, "InputCapture omitted session handle"};
        }
        if (version >= 2) {
            g_variant_unref(values);
            error =
                request(kCapture, "Start",
                        g_variant_new("(os@a{sv})", session_.c_str(), config.parent_window.c_str(),
                                      g_variant_builder_end(&options_builder)),
                        &values);
            if (error) {
                return error;
            }
        }
        guint caps = 0;
        g_variant_lookup(values, "capabilities", "u", &caps);
        g_variant_unref(values);
        capabilities_ = {bool(caps & 1), bool(caps & 2), bool(caps & 2), bool(caps & 2),
                         bool(caps & 4)};
        GUnixFDList* fds = nullptr;
        auto* response = g_dbus_connection_call_with_unix_fd_list_sync(
            bus_, kBus, kPath, kCapture, "ConnectToEIS",
            g_variant_new("(o@a{sv})", session_.c_str(), options()), G_VARIANT_TYPE("(h)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &fds, nullptr, &native_error);
        gint handle = -1;
        if (response) {
            g_variant_get(response, "(h)", &handle);
            g_variant_unref(response);
        }
        const int fd = fds && handle >= 0 ? g_unix_fd_list_get(fds, handle, &native_error) : -1;
        if (fds) {
            g_object_unref(fds);
        }
        if (fd < 0) {
            std::string text =
                native_error ? native_error->message : "InputCapture EIS descriptor missing";
            g_clear_error(&native_error);
            return {ErrorCode::Unavailable, text};
        }
        input_ = api_->ei_new_receiver(nullptr);
        api_->ei_configure_name(input_, "BreInput monitor");
        if (api_->ei_setup_backend_fd(input_, fd) < 0) {
            return {ErrorCode::BackendFailure, "Cannot attach capture EIS socket"};
        }
        signals_ = g_dbus_connection_signal_subscribe(
            bus_, kBus, kCapture, nullptr, kPath, session_.c_str(), G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal,
               GVariant*, gpointer data) {
                auto* self = static_cast<PortalCapture*>(data);
                if (std::string_view(signal) == "Disabled") {
                    self->disabled_ = true;
                } else if (std::string_view(signal) == "ZonesChanged") {
                    self->zones_changed_ = true;
                } else if (std::string_view(signal) == "Deactivated") {
                    self->deactivated_ = true;
                }
            },
            this, nullptr);
        closed_signal_ = g_dbus_connection_signal_subscribe(
            bus_, kBus, "org.freedesktop.portal.Session", "Closed", session_.c_str(), nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*,
               gpointer data) {
                static_cast<PortalCapture*>(data)->disabled_ = true;
            },
            this, nullptr);
        if (auto barrier_error = barriers()) {
            return barrier_error;
        }
        return call("Enable", g_variant_new("(o@a{sv})", session_.c_str(), options()), kCapture);
    }
    Capabilities GetCapabilities() const override { return capabilities_; }
    Error Poll(const EventCallback& enqueue) override {
        while (g_main_context_iteration(context_, false)) {
        }
        if (disabled_) {
            release(enqueue);
            return {ErrorCode::Cancelled, "Compositor disabled controlled input capture"};
        }
        if (deactivated_) {
            release(enqueue);
            deactivated_ = false;
        }
        if (zones_changed_) {
            zones_changed_ = false;
            release(enqueue);
            if (auto error = barriers()) {
                return error;
            }
        }
        pollfd descriptor{api_->ei_get_fd(input_), POLLIN, 0};
        poll(&descriptor, 1, 10);
        api_->ei_dispatch(input_);
        while (auto* event = api_->ei_get_event(input_)) {
            const auto type = api_->ei_event_get_type(event);
            if (type == EI_EVENT_DISCONNECT) {
                api_->ei_event_unref(event);
                release(enqueue);
                return {ErrorCode::Unavailable, "InputCapture EIS connection closed"};
            }
            if (type == EI_EVENT_SEAT_ADDED) {
                api_->ei_seat_bind_capabilities(
                    api_->ei_event_get_seat(event), EI_DEVICE_CAP_KEYBOARD, EI_DEVICE_CAP_POINTER,
                    EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_SCROLL,
                    EI_DEVICE_CAP_TOUCH, nullptr);
            } else if (type == EI_EVENT_DEVICE_STOP_EMULATING || type == EI_EVENT_DEVICE_REMOVED) {
                release(enqueue);
            } else if (type == EI_EVENT_KEYBOARD_KEY) {
                const auto usage = detail::FromEvdev(api_->ei_event_keyboard_get_key(event));
                if (usage) {
                    const bool down = api_->ei_event_keyboard_get_key_is_press(event);
                    if (down) {
                        keys_.insert(usage);
                    } else {
                        keys_.erase(usage);
                    }
                    enqueue(Key{usage, down});
                }
            } else if (type == EI_EVENT_POINTER_MOTION) {
                enqueue(Pointer{CoordinateSpace::RelativeMotion,
                                int(std::lround(api_->ei_event_pointer_get_dx(event))),
                                int(std::lround(api_->ei_event_pointer_get_dy(event)))});
            } else if (type == EI_EVENT_POINTER_MOTION_ABSOLUTE) {
                enqueue(Pointer{CoordinateSpace::Desktop,
                                int(api_->ei_event_pointer_get_absolute_x(event)),
                                int(api_->ei_event_pointer_get_absolute_y(event))});
            } else if (type == EI_EVENT_BUTTON_BUTTON) {
                const auto code = api_->ei_event_button_get_button(event);
                if (code >= 0x110 && code <= 0x114) {
                    const auto button = std::uint8_t(code - 0x110 + 1);
                    const bool down = api_->ei_event_button_get_is_press(event);
                    if (down) {
                        buttons_.insert(button);
                    } else {
                        buttons_.erase(button);
                    }
                    enqueue(Button{button, down});
                }
            } else if (type == EI_EVENT_SCROLL_DISCRETE) {
                enqueue(Scroll{
                    std::clamp(api_->ei_event_scroll_get_discrete_dx(event) / 120, -120, 120),
                    std::clamp(-api_->ei_event_scroll_get_discrete_dy(event) / 120, -120, 120)});
            } else if (type == EI_EVENT_TOUCH_DOWN || type == EI_EVENT_TOUCH_MOTION ||
                       type == EI_EVENT_TOUCH_UP) {
                const auto id = api_->ei_event_touch_get_id(event);
                auto it = touches_.find(id);
                if (type == EI_EVENT_TOUCH_DOWN && touches_.size() < 10) {
                    unsigned slot = 0;
                    for (; slot < 10; ++slot) {
                        if (std::none_of(touches_.begin(), touches_.end(),
                                         [slot](const auto& entry) {
                                             return entry.second.id == slot;
                                         })) {
                            break;
                        }
                    }
                    it = touches_.emplace(id, Touch{TouchPhase::Down, std::uint8_t(slot), 0, 0})
                             .first;
                }
                if (it != touches_.end()) {
                    if (type == EI_EVENT_TOUCH_UP) {
                        it->second.phase = TouchPhase::Up;
                        enqueue(it->second);
                        touches_.erase(it);
                    } else {
                        auto x = api_->ei_event_touch_get_x(event),
                             y = api_->ei_event_touch_get_y(event);
                        auto* region =
                            api_->ei_device_get_region_at(api_->ei_event_get_device(event), x, y);
                        if (region && api_->ei_region_get_width(region) > 1 &&
                            api_->ei_region_get_height(region) > 1) {
                            auto& touch = it->second;
                            touch.phase =
                                type == EI_EVENT_TOUCH_DOWN ? TouchPhase::Down : TouchPhase::Move;
                            touch.x = int((x - api_->ei_region_get_x(region)) * 65535 /
                                          (api_->ei_region_get_width(region) - 1));
                            touch.y = int((y - api_->ei_region_get_y(region)) * 65535 /
                                          (api_->ei_region_get_height(region) - 1));
                            enqueue(touch);
                        }
                    }
                }
            }
            api_->ei_event_unref(event);
        }
        return {};
    }
    void Stop() noexcept override {
        if (bus_) {
            if (signals_) {
                g_dbus_connection_signal_unsubscribe(bus_, signals_);
            }
            if (closed_signal_) {
                g_dbus_connection_signal_unsubscribe(bus_, closed_signal_);
            }
        }
        signals_ = 0;
        closed_signal_ = 0;
        if (input_) {
            api_->ei_unref(input_);
            input_ = nullptr;
        }
        api_.reset();
        close();
        capabilities_ = {};
        keys_.clear();
        buttons_.clear();
        touches_.clear();
        disabled_ = false;
        deactivated_ = false;
        zones_changed_ = false;
    }
};
}  // namespace
std::unique_ptr<CaptureBackend> CreatePortalCaptureBackend() {
    return std::make_unique<PortalCapture>();
}
}  // namespace breinput
