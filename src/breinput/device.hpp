#pragma once

#include <functional>
#include <memory>
#include <stop_token>
#include <string>

#include "event.hpp"

namespace breinput {
enum class ErrorCode {
    Ok,
    InvalidArgument,
    Unsupported,
    PermissionDenied,
    Unavailable,
    NotRunning,
    QueueFull,
    BackendFailure,
    Cancelled
};
struct Error {
    ErrorCode code = ErrorCode::Ok;
    std::string message;
    explicit operator bool() const noexcept { return code != ErrorCode::Ok; }
};
struct Capabilities {
    bool keyboard = false, pointer = false, buttons = false, scroll = false, touch = false;
    bool Supports(const Event&) const noexcept;
};
struct DeviceConfig {
    Rectangle target;           // width/height 为 0 时使用后端桌面范围。
    std::size_t queue_capacity = 256;
    std::string parent_window;  // Portal 父窗口标识，如 wayland:…；为空允许独立授权对话框。
};
using ErrorCallback = std::function<void(const Error&)>;
using EventCallback = std::function<void(const Event&)>;
// 所有方法只在 Injector 的工作线程执行；可以注入测试或应用专属后端。
class InjectionBackend {
public:
    virtual ~InjectionBackend() = default;
    virtual Error Start(const DeviceConfig&, std::stop_token) = 0;
    virtual Error Poll() { return {}; }
    virtual Capabilities GetCapabilities() const = 0;
    virtual Error Inject(const Event&) = 0;
    virtual void Stop() noexcept = 0;
};
std::unique_ptr<InjectionBackend> CreateInjectionBackend();

class Injector final {
public:
    explicit Injector(DeviceConfig config = {}, ErrorCallback on_error = {},
                      std::unique_ptr<InjectionBackend> backend = {});
    ~Injector();
    Injector(const Injector&) = delete;
    Injector& operator=(const Injector&) = delete;
    Error Start();
    // 只排队；队列满明确返回错误，不丢弃按下/释放事件。
    Error Submit(Event event);
    void ReleaseAll();
    // 外部调用等待释放与后端关闭；错误回调内调用仅请求停止，避免等待自身。
    void Stop();
    void WaitForIdle();
    Capabilities GetCapabilities() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
}  // namespace breinput
