#include "monitor.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
namespace breinput {
struct Monitor::State {
    // 复用 Injector 的有界交付线程；此后端只分发事件，不做系统输入注入。
    struct Delivery final : InjectionBackend {
        EventCallback callback;
        std::mutex mutex;
        Error pending;
        void error(Error value) {
            std::lock_guard lock(mutex);
            if (!pending) {
                pending = std::move(value);
            }
        }
        Error Start(const DeviceConfig&, std::stop_token) override {
            std::lock_guard lock(mutex);
            pending = {};
            return {};
        }
        Capabilities GetCapabilities() const override { return {true, true, true, true, true}; }
        Error Inject(const Event& event) override {
            callback(event);
            return {};
        }
        Error Poll() override {
            std::lock_guard lock(mutex);
            return std::exchange(pending, {});
        }
        void Stop() noexcept override {}
    };
    MonitorConfig config;
    std::mutex mutex;
    std::condition_variable cv;
    std::stop_source stop;
    bool active = false, ready = false;
    Error startup;
    Capabilities capabilities;
    std::unique_ptr<CaptureBackend> backend;
    Delivery* delivery = nullptr;
    std::shared_ptr<Injector> dispatch;
    void run() noexcept {
        Error error;
        Capabilities caps;
        try {
            error = backend->Start(config, stop.get_token());
            if (!error) {
                caps = backend->GetCapabilities();
            }
        } catch (const std::exception& e) {
            error = {ErrorCode::BackendFailure, e.what()};
        } catch (...) {
            error = {ErrorCode::BackendFailure, "Capture startup threw"};
        }
        {
            std::lock_guard lock(mutex);
            startup = error;
            ready = true;
            if (!error) {
                capabilities = caps;
            }
            cv.notify_all();
        }
        while (!error && !stop.stop_requested()) {
            try {
                error = backend->Poll([this](const Event& event) {
                    if (auto submit_error = dispatch->Submit(event)) {
                        delivery->error(submit_error);
                        stop.request_stop();
                    }
                });
            } catch (const std::exception& e) {
                error = {ErrorCode::BackendFailure, e.what()};
            } catch (...) {
                error = {ErrorCode::BackendFailure, "Capture polling threw"};
            }
            if (error) {
                delivery->error(error);
            }
        }
        backend->Stop();
        dispatch->ReleaseAll();
        {
            std::lock_guard lock(mutex);
            active = false;
            capabilities = {};
            cv.notify_all();
        }
    }
};
Monitor::Monitor(EventCallback callback, ErrorCallback error, MonitorConfig options,
                 std::unique_ptr<CaptureBackend> backend)
    : state_(std::make_shared<State>()) {
    auto delivery = std::make_unique<State::Delivery>();
    delivery->callback = std::move(callback);
    state_->delivery = delivery.get();
    state_->config = options;
    state_->backend = backend ? std::move(backend) : CreateCaptureBackend(options);
    DeviceConfig config;
    config.queue_capacity = options.queue_capacity;
    state_->dispatch = std::make_shared<Injector>(config, std::move(error), std::move(delivery));
}
Monitor::~Monitor() { Stop(); }
Error Monitor::Start() {
    auto s = state_;
    std::unique_lock lock(s->mutex);
    if (s->active) {
        return {ErrorCode::InvalidArgument, "Monitor already started"};
    }
    if (!s->backend || !s->delivery->callback) {
        return {ErrorCode::InvalidArgument, "Monitor requires callback and backend"};
    }
    if (auto error = s->dispatch->Start()) {
        return error;
    }
    s->stop = std::stop_source{};
    s->active = true;
    s->ready = false;
    s->capabilities = {};
    try {
        std::thread([s] {
            s->run();
        }).detach();
    } catch (const std::exception& e) {
        s->active = false;
        s->dispatch->Stop();
        return {ErrorCode::Unavailable, e.what()};
    }
    s->cv.wait(lock, [&] {
        return s->ready;
    });
    const auto error = s->startup;
    if (error) {
        s->cv.wait(lock, [&] {
            return !s->active;
        });
        s->dispatch->Stop();
    }
    return error;
}
void Monitor::Stop() {
    auto s = state_;
    {
        std::unique_lock lock(s->mutex);
        s->stop.request_stop();
        s->cv.wait(lock, [&] {
            return !s->active;
        });
    }
    s->dispatch->Stop();
}
Capabilities Monitor::GetCapabilities() const {
    std::lock_guard lock(state_->mutex);
    return state_->capabilities;
}
}  // namespace breinput
