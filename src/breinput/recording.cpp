#include "recording.hpp"

#include <condition_variable>
namespace breinput {
void Recorder::Record(Event event) {
    if (!IsValid(event)) {
        return;
    }
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    if (events_.empty()) {
        start_ = now;
    }
    events_.push_back(
        {std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(now - start_)});
}
void Recorder::Clear() {
    std::lock_guard lock(mutex_);
    events_.clear();
}
std::vector<RecordedEvent> Recorder::Snapshot() const {
    std::lock_guard lock(mutex_);
    return events_;
}
Error Playback(Injector& injector, const std::vector<RecordedEvent>& events, std::stop_token stop) {
    std::chrono::microseconds previous{};
    for (const auto& item : events) {
        if (!IsValid(item.event) || item.offset < previous) {
            return {ErrorCode::InvalidArgument, "Invalid recording"};
        }
        previous = item.offset;
    }
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock(mutex);
    const auto start = std::chrono::steady_clock::now();
    for (const auto& item : events) {
        cv.wait_until(lock, stop, start + item.offset, [] {
            return false;
        });
        if (stop.stop_requested()) {
            injector.ReleaseAll();
            return {ErrorCode::Cancelled, "Playback cancelled"};
        }
        if (auto error = injector.Submit(item.event)) {
            injector.ReleaseAll();
            return error;
        }
    }
    injector.WaitForIdle();
    injector.ReleaseAll();
    injector.WaitForIdle();
    return {};
}
}  // namespace breinput
