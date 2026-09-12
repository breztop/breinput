#include "device.hpp"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace breinput {
struct Injector::State {
    DeviceConfig config;
    ErrorCallback on_error;
    std::unique_ptr<InjectionBackend> backend;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::optional<Event>> queue;
    bool active = false, ready = false, stopping = false, busy = false;
    std::thread::id worker;
    Error startup;
    std::stop_source stop;
    Capabilities capabilities;
    std::set<std::uint16_t> keys;
    std::set<std::uint8_t> buttons;
    std::map<std::uint8_t, Touch> touches;

    void report(const Error& error) noexcept {
        if (error && on_error) {
            try {
                on_error(error);
            } catch (...) {
            }
        }
    }
    void release() noexcept {
        auto release_event = [this](Event event) {
            try {
                report(backend->Inject(event));
            } catch (...) {
                report({ErrorCode::BackendFailure, "Backend threw while releasing input"});
            }
        };
        // 单项释放失败不能阻止其余按键、按钮和触点释放。
        for (auto key : keys) {
            release_event(Key{key, false});
        }
        for (auto button : buttons) {
            release_event(Button{button, false});
        }
        for (auto [id, touch] : touches) {
            touch.phase = TouchPhase::Cancel;
            release_event(touch);
        }
        keys.clear();
        buttons.clear();
        touches.clear();
    }
    void track(const Event& event, bool delivered) {
        if (const auto* key = std::get_if<Key>(&event)) {
            if (key->down) {
                keys.insert(key->usage);
            } else if (delivered) {
                keys.erase(key->usage);
            }
        } else if (const auto* button = std::get_if<Button>(&event)) {
            if (button->down) {
                buttons.insert(button->button);
            } else if (delivered) {
                buttons.erase(button->button);
            }
        } else if (const auto* touch = std::get_if<Touch>(&event)) {
            if (touch->phase == TouchPhase::Up || touch->phase == TouchPhase::Cancel) {
                if (delivered) {
                    touches.erase(touch->id);
                }
            } else {
                touches[touch->id] = *touch;
            }
        }
    }
    void run() noexcept {
        Error error;
        Capabilities initial_capabilities;
        try {
            error = backend->Start(config, stop.get_token());
            if (!error) {
                initial_capabilities = backend->GetCapabilities();
            }
        } catch (const std::exception& e) {
            error = {ErrorCode::BackendFailure, e.what()};
        } catch (...) {
            error = {ErrorCode::BackendFailure, "Backend startup threw"};
        }
        {
            std::lock_guard lock(mutex);
            worker = std::this_thread::get_id();
            startup = error;
            ready = true;
            if (!error) {
                capabilities = initial_capabilities;
            }
            cv.notify_all();
        }
        if (!error) {
            for (;;) {
                std::optional<Event> event;
                {
                    std::unique_lock lock(mutex);
                    cv.wait_for(lock, std::chrono::milliseconds(10), [this] {
                        return stopping || !queue.empty();
                    });
                    if (stopping) {
                        break;
                    }
                    if (queue.empty()) {
                        lock.unlock();
                        Capabilities caps;
                        try {
                            report(backend->Poll());
                            caps = backend->GetCapabilities();
                        } catch (...) {
                            report({ErrorCode::BackendFailure, "Input polling failed"});
                        }
                        lock.lock();
                        capabilities = caps;
                        continue;
                    }
                    event = std::move(queue.front());
                    queue.pop_front();
                    busy = true;
                }
                if (!event) {
                    release();
                } else {
                    // 即使后端只完成了部分注入，也保留释放责任。
                    track(*event, false);
                    try {
                        error = backend->Inject(*event);
                    } catch (const std::exception& e) {
                        error = {ErrorCode::BackendFailure, e.what()};
                    } catch (...) {
                        error = {ErrorCode::BackendFailure, "Backend injection threw"};
                    }
                    if (!error) {
                        track(*event, true);
                    }
                    report(error);
                }
                {
                    std::lock_guard lock(mutex);
                    busy = false;
                    cv.notify_all();
                }
            }
            release();
        }
        backend->Stop();
        {
            std::lock_guard lock(mutex);
            queue.clear();
            active = false;
            capabilities = {};
            busy = false;
            worker = {};
            cv.notify_all();
        }
    }
};
Injector::Injector(DeviceConfig config, ErrorCallback callback,
                   std::unique_ptr<InjectionBackend> backend)
    : state_(std::make_shared<State>()) {
    state_->config = std::move(config);
    state_->on_error = std::move(callback);
    state_->backend = backend ? std::move(backend) : CreateInjectionBackend();
}
Injector::~Injector() { Stop(); }
Error Injector::Start() {
    const auto s = state_;
    std::unique_lock lock(s->mutex);
    if (s->active) {
        return {ErrorCode::InvalidArgument, "Injector is already started or stopping"};
    }
    if (s->config.queue_capacity < 2 || s->config.queue_capacity > 65536 || !s->backend ||
        s->config.target.width < 0 || s->config.target.height < 0 ||
        ((s->config.target.width == 0) != (s->config.target.height == 0))) {
        return {ErrorCode::InvalidArgument, "Invalid input configuration"};
    }
    s->stop = std::stop_source{};
    s->active = true;
    s->ready = false;
    s->stopping = false;
    s->capabilities = {};
    // 工作线程只持有共享状态，不访问 Injector。外部 Stop 通过 cv 等待退出；
    // 回调可以销毁 Injector，状态在释放和平台资源关闭后才销毁。
    try {
        std::thread([s] {
            s->run();
        }).detach();
    } catch (const std::exception& e) {
        s->active = false;
        return {ErrorCode::Unavailable, e.what()};
    }
    s->cv.wait(lock, [&] {
        return s->ready;
    });
    if (s->startup) {
        s->cv.wait(lock, [&] {
            return !s->active;
        });
    }
    return s->startup;
}
Error Injector::Submit(Event event) {
    if (!IsValid(event)) {
        return {ErrorCode::InvalidArgument, "Invalid input event"};
    }
    const auto s = state_;
    std::lock_guard lock(s->mutex);
    if (!s->active || !s->ready || s->stopping || s->startup) {
        return {ErrorCode::NotRunning, "Injector is stopped"};
    }
    if (!s->capabilities.Supports(event)) {
        return {ErrorCode::Unsupported, "Input kind is unsupported by this backend"};
    }
    // 仅相邻绝对移动可以覆盖；相对移动不能覆盖（否则丢失位移）。
    if (auto* pointer = std::get_if<Pointer>(&event);
        pointer && pointer->space != CoordinateSpace::RelativeMotion && !s->queue.empty() &&
        s->queue.back()) {
        if (auto* last = std::get_if<Pointer>(&*s->queue.back());
            last && last->space == pointer->space) {
            s->queue.back() = std::move(event);
            return {};
        }
    }
    if (s->queue.size() >= s->config.queue_capacity) {
        return {ErrorCode::QueueFull, "Input queue is full; release or disable the session"};
    }
    s->queue.emplace_back(std::move(event));
    s->cv.notify_all();
    return {};
}
void Injector::ReleaseAll() {
    const auto s = state_;
    std::lock_guard lock(s->mutex);
    if (s->active && !s->stopping) {
        s->queue.clear();
        s->queue.emplace_back(std::nullopt);
        s->cv.notify_all();
    }
}
void Injector::Stop() {
    const auto s = state_;
    std::unique_lock lock(s->mutex);
    s->stop.request_stop();
    s->stopping = true;
    s->queue.clear();
    s->cv.notify_all();
    if (s->worker != std::this_thread::get_id()) {
        s->cv.wait(lock, [&] {
            return !s->active;
        });
    }
}
void Injector::WaitForIdle() {
    const auto s = state_;
    std::unique_lock lock(s->mutex);
    if (s->worker != std::this_thread::get_id()) {
        s->cv.wait(lock, [&] {
            return !s->active || (s->queue.empty() && !s->busy);
        });
    }
}
Capabilities Injector::GetCapabilities() const {
    std::lock_guard lock(state_->mutex);
    return state_->capabilities;
}
}  // namespace breinput
