#pragma once
#include <chrono>
#include <mutex>
#include <stop_token>
#include <vector>

#include "device.hpp"
namespace breinput {
struct RecordedEvent {
    Event event;
    std::chrono::microseconds offset;
};
class Recorder final {
public:
    void Record(Event event);
    void Clear();
    std::vector<RecordedEvent> Snapshot() const;

private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point start_;
    std::vector<RecordedEvent> events_;
};
// 在调用线程回放；stop_token 中断等待，并释放本次回放按键。注入器由调用方启动。
Error Playback(Injector& injector, const std::vector<RecordedEvent>& events,
               std::stop_token stop = {});
}  // namespace breinput
