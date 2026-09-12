#pragma once
#include <gio/gio.h>

#include <chrono>
#include <thread>

#include "device.hpp"
namespace breinput::detail {
inline constexpr auto kBus = "org.freedesktop.portal.Desktop";
inline constexpr auto kPath = "/org/freedesktop/portal/desktop";
inline constexpr auto kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
// 每个会话独占 D-Bus 连接和 GMainContext。只在后端线程调用。
class PortalConnection {
protected:
    GDBusConnection* bus_ = nullptr;
    GMainContext* context_ = nullptr;
    std::string session_;
    std::stop_token stop_;
    static GVariant* options() {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        return g_variant_builder_end(&b);
    }
    Error call(const char* method, GVariant* args, const char* interface = kRemoteDesktop) {
        GError* error = nullptr;
        auto* result =
            g_dbus_connection_call_sync(bus_, kBus, kPath, interface, method, args, nullptr,
                                        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (!result) {
            std::string text = error ? error->message : "Portal call failed";
            g_clear_error(&error);
            return {ErrorCode::BackendFailure, text};
        }
        g_variant_unref(result);
        return {};
    }
    Error request(const char* interface, const char* method, GVariant* args,
                  GVariant** values = nullptr) {
        struct Reply {
            bool ready = false;
            guint code = 2;
            GVariant* values = nullptr;
            ~Reply() {
                if (values) {
                    g_variant_unref(values);
                }
            }
        } reply;
        // 本连接只有一项未完成的 portal 请求。先订阅再调用，避免快速响应的竞态。
        const auto subscription = g_dbus_connection_signal_subscribe(
            bus_, kBus, "org.freedesktop.portal.Request", "Response", nullptr, nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
               GVariant* response_args, gpointer data) {
                auto& r = *static_cast<Reply*>(data);
                if (!r.ready) {
                    g_variant_get(response_args, "(u@a{sv})", &r.code, &r.values);
                    r.ready = true;
                }
            },
            &reply, nullptr);
        GError* error = nullptr;
        auto* result = g_dbus_connection_call_sync(bus_, kBus, kPath, interface, method, args,
                                                   G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE,
                                                   5000, nullptr, &error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (result && !reply.ready && !stop_.stop_requested() &&
               std::chrono::steady_clock::now() < deadline) {
            while (g_main_context_iteration(context_, false)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        g_dbus_connection_signal_unsubscribe(bus_, subscription);
        if (result && !reply.ready) {
            const char* request_path = nullptr;
            g_variant_get(result, "(&o)", &request_path);
            g_dbus_connection_call(bus_, kBus, request_path, "org.freedesktop.portal.Request",
                                   "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
                                   nullptr, nullptr);
        }
        if (result) {
            g_variant_unref(result);
        }
        if (error) {
            std::string text = error->message;
            g_error_free(error);
            return {ErrorCode::Unavailable, text};
        }
        if (stop_.stop_requested()) {
            return {ErrorCode::Cancelled, "Portal authorization cancelled"};
        }
        if (!reply.ready || reply.code != 0) {
            return {ErrorCode::PermissionDenied,
                    "Portal authorization cancelled, denied or timed out"};
        }
        if (values) {
            *values = g_variant_ref(reply.values);
        }
        return {};
    }

    Error open(std::stop_token stop) {
        stop_ = stop;
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);
        GError* error = nullptr;
        // 私有连接隔离 Request/Response，多个输入会话不会接收彼此的授权结果。
        auto* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (address) {
            bus_ = g_dbus_connection_new_for_address_sync(
                address,
                GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                nullptr, nullptr, &error);
            g_free(address);
        }
        if (!bus_) {
            std::string text = error ? error->message : "Session bus unavailable";
            g_clear_error(&error);
            return {ErrorCode::Unavailable, text};
        }
        return {};
    }
    void close() noexcept {
        if (bus_) {
            if (!session_.empty()) {
                auto* result = g_dbus_connection_call_sync(
                    bus_, kBus, session_.c_str(), "org.freedesktop.portal.Session", "Close",
                    nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
                if (result) {
                    g_variant_unref(result);
                }
            }
            g_dbus_connection_close_sync(bus_, nullptr, nullptr);
            g_object_unref(bus_);
            bus_ = nullptr;
        }
        session_.clear();
        if (context_) {
            g_main_context_pop_thread_default(context_);
            g_main_context_unref(context_);
            context_ = nullptr;
        }
    }
};
}  // namespace breinput::detail
