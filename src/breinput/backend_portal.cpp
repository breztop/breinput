#include <gio/gio.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include "device.hpp"
#include "eis_sender.hpp"
#include "key_map.hpp"
#include "portal_connection.hpp"

namespace breinput {
namespace {
constexpr auto kBus = "org.freedesktop.portal.Desktop";
constexpr auto kPath = "/org/freedesktop/portal/desktop";
constexpr auto kInterface = "org.freedesktop.portal.RemoteDesktop";
class PortalBackend final : public InjectionBackend, private detail::PortalConnection {
    std::string mapping_id_;
    std::unique_ptr<detail::EisSender> eis_;
    Capabilities capabilities_;
    guint closed_subscription_ = 0;
    bool session_closed_ = false;
    bool closed_reported_ = false;
    std::uint32_t stream_ = 0;
    int width_ = 0, height_ = 0;

public:
    Error Start(const DeviceConfig& config, std::stop_token stop) override {
        if (auto error = open(stop)) {
            return error;
        }

        GVariant* values = nullptr;
        auto status =
            request(kInterface, "CreateSession", g_variant_new("(@a{sv})", options()), &values);
        if (status) {
            return status;
        }
        const char* path = nullptr;
        g_variant_lookup(values, "session_handle", "&s", &path);
        if (path) {
            session_ = path;
        }
        g_variant_unref(values);
        if (session_.empty()) {
            return {ErrorCode::BackendFailure, "Portal omitted session handle"};
        }
        closed_subscription_ = g_dbus_connection_signal_subscribe(
            bus_, kBus, "org.freedesktop.portal.Session", "Closed", session_.c_str(), nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*,
               gpointer data) {
                static_cast<PortalBackend*>(data)->session_closed_ = true;
            },
            this, nullptr);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "types", g_variant_new_uint32(7));
        status = request(kInterface, "SelectDevices",
                         g_variant_new("(o@a{sv})", session_.c_str(), g_variant_builder_end(&b)));
        if (status) {
            return status;
        }
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "types", g_variant_new_uint32(1));
        g_variant_builder_add(&b, "{sv}", "multiple", g_variant_new_boolean(false));
        status = request("org.freedesktop.portal.ScreenCast", "SelectSources",
                         g_variant_new("(o@a{sv})", session_.c_str(), g_variant_builder_end(&b)));
        if (status) {
            return status;
        }
        status = request(
            kInterface, "Start",
            g_variant_new("(os@a{sv})", session_.c_str(), config.parent_window.c_str(), options()),
            &values);
        if (status) {
            return status;
        }
        guint devices = 0;
        g_variant_lookup(values, "devices", "u", &devices);
        auto* streams = g_variant_lookup_value(values, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (streams && g_variant_n_children(streams) > 0) {
            GVariant* properties = nullptr;
            g_variant_get_child(streams, 0, "(u@a{sv})", &stream_, &properties);
            if (!g_variant_lookup(properties, "logical_size", "(ii)", &width_, &height_)) {
                g_variant_lookup(properties, "size", "(ii)", &width_, &height_);
            }
            const char* mapping = nullptr;
            if (g_variant_lookup(properties, "mapping_id", "&s", &mapping)) {
                mapping_id_ = mapping;
            }
            g_variant_unref(properties);
        }
        if (streams) {
            g_variant_unref(streams);
        }
        g_variant_unref(values);
        capabilities_ = {bool(devices & 1), bool(devices & 2), bool(devices & 2), bool(devices & 2),
                         bool(devices & 4) && stream_ != 0};
        Error load_error;
        auto api = detail::EiLibrary::Load(load_error);
        if (!api) {
            return {};
        }  // 未连接 EIS，继续使用 Portal Notify。
        GError* error = nullptr;
        GUnixFDList* descriptors = nullptr;
        auto* response = g_dbus_connection_call_with_unix_fd_list_sync(
            bus_, kBus, kPath, kInterface, "ConnectToEIS",
            g_variant_new("(o@a{sv})", session_.c_str(), options()), G_VARIANT_TYPE("(h)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &descriptors, nullptr, &error);
        if (response) {
            gint handle = -1;
            g_variant_get(response, "(h)", &handle);
            g_variant_unref(response);
            const int fd = descriptors ? g_unix_fd_list_get(descriptors, handle, &error) : -1;
            if (descriptors) {
                g_object_unref(descriptors);
            }
            if (fd < 0) {
                std::string text = error ? error->message : "EIS descriptor missing";
                g_clear_error(&error);
                return {ErrorCode::BackendFailure, text};
            }
            eis_ = std::make_unique<detail::EisSender>(std::move(api));
            return eis_->Start(fd, mapping_id_, stop);
        }
        // 未建立 EIS 时，旧版 Portal 才允许继续使用 Notify API。
        if (descriptors) {
            g_object_unref(descriptors);
        }
        if (error && !g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
            std::string text = error->message;
            g_clear_error(&error);
            return {ErrorCode::Unavailable, text};
        }
        g_clear_error(&error);
        return {};
    }
    Capabilities GetCapabilities() const override {
        if (session_closed_) {
            return {};
        }
        if (eis_) {
            return eis_->GetCapabilities();
        }
        return capabilities_;
    }
    Error Poll() override {
        while (context_ && g_main_context_iteration(context_, false)) {
        }
        if (session_closed_ && !closed_reported_) {
            closed_reported_ = true;
            return {ErrorCode::Unavailable, "Portal input session closed"};
        }
        if (eis_) {
            return eis_->Poll();
        }
        return {};
    }
    Error Inject(const Event& event) override {
        if (auto error = Poll()) {
            return error;
        }
        if (session_closed_) {
            return {ErrorCode::NotRunning, "Portal input session closed"};
        }
        if (eis_) {
            return eis_->Inject(event);
        }
        return std::visit(
            [this](const auto& v) -> Error {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Key>) {
                    const auto code = detail::kEvdev[v.usage];
                    if (!code) {
                        return {ErrorCode::Unsupported, "HID key has no evdev mapping"};
                    }
                    return call("NotifyKeyboardKeycode",
                                g_variant_new("(o@a{sv}iu)", session_.c_str(), options(), int(code),
                                              guint(v.down)));
                } else if constexpr (std::is_same_v<T, Pointer>) {
                    if (v.space == CoordinateSpace::RelativeMotion) {
                        return call("NotifyPointerMotion",
                                    g_variant_new("(o@a{sv}dd)", session_.c_str(), options(),
                                                  double(v.x), double(v.y)));
                    }
                    if (v.space != CoordinateSpace::Normalized || !stream_ || width_ <= 0 ||
                        height_ <= 0) {
                        return {ErrorCode::Unsupported,
                                "Portal absolute input requires the selected stream's normalized "
                                "coordinates"};
                    }
                    return call("NotifyPointerMotionAbsolute",
                                g_variant_new("(o@a{sv}udd)", session_.c_str(), options(), stream_,
                                              double(v.x) * (width_ - 1) / 65535,
                                              double(v.y) * (height_ - 1) / 65535));
                } else if constexpr (std::is_same_v<T, Button>) {
                    constexpr int buttons[] = {0, 0x110, 0x111, 0x112, 0x113, 0x114};
                    return call("NotifyPointerButton",
                                g_variant_new("(o@a{sv}iu)", session_.c_str(), options(),
                                              buttons[v.button], guint(v.down)));
                } else if constexpr (std::is_same_v<T, Scroll>) {
                    Error error;
                    if (v.x) {
                        error = call("NotifyPointerAxisDiscrete",
                                     g_variant_new("(o@a{sv}ui)", session_.c_str(), options(),
                                                   guint(1), v.x));
                    }
                    if (!error && v.y) {
                        error = call("NotifyPointerAxisDiscrete",
                                     g_variant_new("(o@a{sv}ui)", session_.c_str(), options(),
                                                   guint(0), -v.y));
                    }
                    return error;
                } else {
                    if (!stream_ || width_ <= 0 || height_ <= 0) {
                        return {ErrorCode::Unsupported, "Portal touch stream has no geometry"};
                    }
                    if (v.phase == TouchPhase::Up || v.phase == TouchPhase::Cancel) {
                        return call("NotifyTouchUp", g_variant_new("(o@a{sv}u)", session_.c_str(),
                                                                   options(), guint(v.id)));
                    }
                    return call(
                        v.phase == TouchPhase::Down ? "NotifyTouchDown" : "NotifyTouchMotion",
                        g_variant_new("(o@a{sv}uudd)", session_.c_str(), options(), stream_,
                                      guint(v.id), double(v.x) * (width_ - 1) / 65535,
                                      double(v.y) * (height_ - 1) / 65535));
                }
            },
            event);
    }
    void Stop() noexcept override {
        eis_.reset();
        if (bus_ && closed_subscription_) {
            g_dbus_connection_signal_unsubscribe(bus_, closed_subscription_);
        }
        closed_subscription_ = 0;
        session_closed_ = false;
        closed_reported_ = false;
        width_ = 0;
        height_ = 0;
        mapping_id_.clear();
        close();
        capabilities_ = {};
        stream_ = 0;
    }
};
}  // namespace
std::unique_ptr<InjectionBackend> CreatePortalBackend() {
    return std::make_unique<PortalBackend>();
}
}  // namespace breinput
