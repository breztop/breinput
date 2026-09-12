#pragma once
#include "device.hpp"
namespace breinput {
enum class CaptureMode { Global, Controlled };
struct MonitorConfig {
    std::size_t queue_capacity = 256;
    CaptureMode mode = CaptureMode::Global;
    std::string parent_window;
};
// Controlled 在 Wayland 上经授权后，用最右侧屏幕边缘激活；由合成器决定何时交付事件。
// 平台采集后端只把事件交给内部队列，不在原生 hook 回调里执行用户代码。
class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual Error Start(const MonitorConfig&, std::stop_token) = 0;
    virtual Error Poll(const EventCallback& enqueue) = 0;
    virtual Capabilities GetCapabilities() const = 0;
    virtual void Stop() noexcept = 0;
};
std::unique_ptr<CaptureBackend> CreateCaptureBackend(const MonitorConfig&);
class Monitor final {
public:
    explicit Monitor(EventCallback callback, ErrorCallback error = {}, MonitorConfig config = {},
                     std::unique_ptr<CaptureBackend> backend = {});
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    Error Start();
    void Stop();
    Capabilities GetCapabilities() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
}  // namespace breinput
