#define BOOST_TEST_MODULE BreInput
#include <atomic>
#include <boost/test/included/unit_test.hpp>
#include <breinput/device.hpp>
#include <breinput/key_mapping.hpp>
#include <breinput/monitor.hpp>
#include <breinput/recording.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace breinput;
struct Fake final : InjectionBackend {
    std::vector<Event> events;
    Error startup;
    bool fail = false, closed = false;
    Error Start(const DeviceConfig&, std::stop_token) override {
        closed = false;
        return startup;
    }
    Capabilities GetCapabilities() const override { return {true, true, true, true, true}; }
    Error Inject(const Event& event) override {
        events.push_back(event);
        return fail ? Error{ErrorCode::BackendFailure, "injected failure"} : Error{};
    }
    void Stop() noexcept override { closed = true; }
};
BOOST_AUTO_TEST_CASE(named_chord_recording_playback_and_release) {
    auto fake = std::make_unique<Fake>();
    auto* backend = fake.get();
    Injector input({}, {}, std::move(fake));
    Recorder recorder;
    recorder.Record(Key{KeyCode::LeftWin, true});
    recorder.Record(Key{KeyCode::A, true});
    recorder.Record(Key{KeyCode::A, false});
    recorder.Record(Button{MouseButton::Left, true});
    BOOST_REQUIRE(!input.Start());
    BOOST_REQUIRE(!Playback(input, recorder.Snapshot()));
    input.WaitForIdle();
    input.Stop();
    BOOST_REQUIRE_EQUAL(backend->events.size(), 6);
    const Key meta_down{KeyCode::LeftCommand, true};
    const Key a_down{KeyCode::A, true};
    const Key a_up{KeyCode::A, false};
    const Button left_down{MouseButton::Left, true};
    const Key meta_up{KeyCode::LeftMeta, false};
    const Button left_up{MouseButton::Left, false};
    BOOST_CHECK(std::get<Key>(backend->events[0]) == meta_down);
    BOOST_CHECK(std::get<Key>(backend->events[1]) == a_down);
    BOOST_CHECK(std::get<Key>(backend->events[2]) == a_up);
    BOOST_CHECK(std::get<Button>(backend->events[3]) == left_down);
    BOOST_CHECK(std::get<Key>(backend->events[4]) == meta_up);
    BOOST_CHECK(std::get<Button>(backend->events[5]) == left_up);
}
BOOST_AUTO_TEST_CASE(release_and_restart) {
    auto fake = std::make_unique<Fake>();
    auto* backend = fake.get();
    Injector input({}, {}, std::move(fake));
    BOOST_CHECK(!input.Start());
    BOOST_CHECK(input.Start());
    BOOST_CHECK(!input.Submit(Key{4, true}));
    BOOST_CHECK(!input.Submit(Button{2, true}));
    BOOST_CHECK(!input.Submit(Touch{TouchPhase::Down, 3, 12, 40}));
    input.WaitForIdle();
    input.Stop();
    input.Stop();
    BOOST_REQUIRE_EQUAL(backend->events.size(), 6);
    BOOST_CHECK(backend->closed);
    BOOST_CHECK(!std::get<Key>(backend->events[3]).down);
    BOOST_CHECK(std::get<Touch>(backend->events[5]).phase == TouchPhase::Cancel);
    BOOST_CHECK(!input.Start());
    input.Stop();
    BOOST_CHECK(input.Submit(Key{4, true}));
}
BOOST_AUTO_TEST_CASE(start_failure_and_error_callback_stop) {
    auto fake = std::make_unique<Fake>();
    fake->startup = {ErrorCode::PermissionDenied, "denied"};
    auto* backend = fake.get();
    Injector input({}, {}, std::move(fake));
    BOOST_CHECK(input.Start());
    BOOST_CHECK(backend->closed);
    input.Stop();
    auto failing = std::make_unique<Fake>();
    failing->fail = true;
    Injector* instance = nullptr;
    Injector second(
        {},
        [&](const Error&) {
            instance->Stop();
            throw 1;
        },
        std::move(failing));
    instance = &second;
    BOOST_CHECK(!second.Start());
    BOOST_CHECK(!second.Submit(Key{4, true}));
    second.WaitForIdle();
    second.Stop();
}
BOOST_AUTO_TEST_CASE(mapping_and_validation) {
    Pointer point;
    BOOST_CHECK(!MapPoint({0, 0, 100, 100}, 200, 100, 0, 50, 10, point));
    BOOST_CHECK(MapPoint({0, 0, 100, 100}, 200, 100, 0, 50, 50, point));
    BOOST_CHECK_EQUAL(point.x, 32768);
    BOOST_CHECK(MapPoint({0, 0, 100, 100}, 200, 100, 0, -100, 50, point, true));
    BOOST_CHECK_EQUAL(point.x, 0);
    BOOST_CHECK(IsValid(Pointer{CoordinateSpace::Desktop, -1920, -500}));
    BOOST_CHECK(!IsValid(Touch{TouchPhase::Down, 10, 0, 0}));
    BOOST_CHECK(!IsValid(Key{65535, true}));
    BOOST_CHECK(!IsValid(Scroll{0, INT32_MIN}));
}
BOOST_AUTO_TEST_CASE(playback_cancel) {
    auto fake = std::make_unique<Fake>();
    Injector input({}, {}, std::move(fake));
    BOOST_REQUIRE(!input.Start());
    std::stop_source stop;
    std::jthread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        stop.request_stop();
    });
    auto result = Playback(
        input, {{Key{4, true}, std::chrono::seconds(0)}, {Key{4, false}, std::chrono::seconds(30)}},
        stop.get_token());
    BOOST_CHECK(result.code == ErrorCode::Cancelled);
    input.Stop();
}

BOOST_AUTO_TEST_CASE(queue_budget_preserves_release_responsibility) {
    struct Blocking final : InjectionBackend {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false, proceed = false;
        std::vector<Event> events;
        Error Start(const DeviceConfig&, std::stop_token) override { return {}; }
        Capabilities GetCapabilities() const override { return {true, true, true, true, true}; }
        Error Inject(const Event& event) override {
            std::unique_lock lock(mutex);
            events.push_back(event);
            if (events.size() == 1) {
                entered = true;
                cv.notify_all();
                cv.wait(lock, [&] {
                    return proceed;
                });
            }
            return {};
        }
        void Stop() noexcept override {}
    };
    auto fake = std::make_unique<Blocking>();
    auto* backend = fake.get();
    DeviceConfig config;
    config.queue_capacity = 2;
    Injector input(config, {}, std::move(fake));
    BOOST_REQUIRE(!input.Start());
    BOOST_REQUIRE(!input.Submit(Key{4, true}));
    {
        std::unique_lock lock(backend->mutex);
        BOOST_REQUIRE(backend->cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return backend->entered;
        }));
    }
    BOOST_CHECK(!input.Submit(Pointer{CoordinateSpace::Normalized, 100, 100}));
    BOOST_CHECK(!input.Submit(Pointer{CoordinateSpace::Normalized, 200, 200}));
    BOOST_CHECK(!input.Submit(Key{5, true}));
    BOOST_CHECK(input.Submit(Key{4, false}).code == ErrorCode::QueueFull);
    input.ReleaseAll();
    {
        std::lock_guard lock(backend->mutex);
        backend->proceed = true;
        backend->cv.notify_all();
    }
    input.WaitForIdle();
    input.Stop();
    BOOST_REQUIRE_EQUAL(backend->events.size(), 2);
    BOOST_CHECK(std::get<Key>(backend->events.back()) == Key({4, false}));
}
BOOST_AUTO_TEST_CASE(monitor_callback_can_stop_and_throw) {
    struct Capture final : CaptureBackend {
        bool delivered = false;
        Error Start(const MonitorConfig&, std::stop_token) override { return {}; }
        Capabilities GetCapabilities() const override { return {true, false, false, false, false}; }
        Error Poll(const EventCallback& enqueue) override {
            if (!delivered) {
                delivered = true;
                enqueue(Key{KeyCode::LeftCommand, true});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return {};
        }
        void Stop() noexcept override {}
    };
    Monitor* owner = nullptr;
    std::atomic<bool> called = false;
    Monitor monitor(
        [&](const Event& event) {
            owner->Stop();
            const auto* key = std::get_if<Key>(&event);
            if (key && key->GetCode() == KeyCode::LeftWin && key->down) {
                called = true;
            }
            throw 1;
        },
        {}, {}, std::make_unique<Capture>());
    owner = &monitor;
    BOOST_REQUIRE(!monitor.Start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!called && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    BOOST_CHECK(called);
    monitor.Stop();
}

BOOST_AUTO_TEST_CASE(unmapped_scan_does_not_become_a_key) {
    BOOST_CHECK_EQUAL(HidFromWindowsScanCode(0), 0);
    BOOST_CHECK_EQUAL(HidFromWindowsScanCode(0xffff), 0);
    BOOST_CHECK_EQUAL(HidFromWindowsScanCode(WindowsScanCodeFromHid(4)), 4);
    BOOST_CHECK_EQUAL(HidFromWindowsScanCode(WindowsScanCodeFromHid(228)), 228);
    BOOST_CHECK_EQUAL(HidFromMacKeyCode(MacKeyCodeFromHid(4)), 4);
}
BOOST_AUTO_TEST_CASE(release_exception_does_not_skip_remaining_inputs) {
    struct Backend final : InjectionBackend {
        std::vector<Event> events;
        Error Start(const DeviceConfig&, std::stop_token) override { return {}; }
        Capabilities GetCapabilities() const override { return {true, true, true, true, true}; }
        Error Inject(const Event& event) override {
            events.push_back(event);
            if (auto* key = std::get_if<Key>(&event); key && !key->down) {
                throw std::runtime_error("release failure");
            }
            return {};
        }
        void Stop() noexcept override {}
    };
    auto backend = std::make_unique<Backend>();
    auto* observed = backend.get();
    std::atomic<unsigned> errors = 0;
    Injector input(
        {},
        [&](const Error&) {
            ++errors;
        },
        std::move(backend));
    BOOST_REQUIRE(!input.Start());
    BOOST_REQUIRE(!input.Submit(Key{4, true}));
    BOOST_REQUIRE(!input.Submit(Key{5, true}));
    BOOST_REQUIRE(!input.Submit(Button{1, true}));
    input.WaitForIdle();
    input.Stop();
    BOOST_REQUIRE_EQUAL(observed->events.size(), 6);
    BOOST_CHECK_EQUAL(errors.load(), 2);
    BOOST_CHECK(!std::get<Button>(observed->events.back()).down);
    BOOST_CHECK(!input.GetCapabilities().keyboard);
}
BOOST_AUTO_TEST_CASE(startup_can_be_cancelled_and_closed) {
    struct Backend final : InjectionBackend {
        std::atomic<bool> entered = false, closed = false;
        Error Start(const DeviceConfig&, std::stop_token stop) override {
            entered = true;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {ErrorCode::Cancelled, "cancelled"};
        }
        Capabilities GetCapabilities() const override { return {}; }
        Error Inject(const Event&) override { return {}; }
        void Stop() noexcept override { closed = true; }
    };
    auto backend = std::make_unique<Backend>();
    auto* observed = backend.get();
    Injector input({}, {}, std::move(backend));
    Error startup;
    std::jthread starter([&] {
        startup = input.Start();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!observed->entered && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    input.Stop();
    starter.join();
    BOOST_CHECK(startup.code == ErrorCode::Cancelled);
    BOOST_CHECK(observed->closed);
}
