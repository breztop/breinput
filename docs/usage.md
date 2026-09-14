# Injection, monitoring, and recording

[中文](usage.zh-CN.md) · [Home](../README.md) · [Build and integration](build.md) · [Key mapping](key-mapping.md)

## Press and release A

Run this example in a supported desktop environment with the required
[platform permissions](build.md).

```cpp
#include <atomic>
#include <breinput/device.hpp>
#include <iostream>

int main() {
    std::atomic<bool> failed = false;
    breinput::Injector input({}, [&](const breinput::Error& error) {
        std::cerr << error.message << '\n';
        failed = true;
    });
    if (auto error = input.Start()) {
        std::cerr << error.message << '\n';
        return 1;
    }
    const breinput::Key events[] = {
        {breinput::KeyCode::A, true},
        {breinput::KeyCode::A, false},
    };
    for (const auto& event : events) {
        if (auto error = input.Submit(event)) {
            std::cerr << error.message << '\n';
            input.Stop();
            return 1;
        }
    }
    input.WaitForIdle();
    input.Stop();
    return failed ? 1 : 0;
}
```

`KeyCode::A` identifies the physical A key. The resulting character depends on the
target application, keyboard layout, modifiers, and input method state.
The library currently has no API for directly entering Unicode text.

## Sessions and error handling

| API | Behavior |
| --- | --- |
| `Start()` | Establishes a backend session and returns startup errors; the constructor itself does not access input devices |
| `Submit(event)` | Checks arguments, session state, and event type, then adds the event to a bounded queue |
| `WaitForIdle()` | Waits for the current queue to finish or the session to end; does not mean the target application has processed the input |
| `ReleaseAll()` | Clears pending events and queues a request to release inputs held by this instance |
| `Stop()` | Clears pending events, releases inputs held by this instance, and closes the session |
| `GetCapabilities()` | Queries the event types supported by the current session |

An `Error` converting to `true` indicates failure. Direct errors from `Start()` and
`Submit()` are returned to the caller; actual injection errors on the worker thread
are reported through the error callback. A successful `Submit()` only means the event
was accepted. Specific keys, coordinate modes, or devices may still be unsupported,
so capability queries alone are not sufficient.

The default queue capacity is 256, configurable through `DeviceConfig::queue_capacity`.
Only consecutive absolute moves in the same coordinate mode are merged; relative
movement and the order of press and release events are preserved. A full queue returns
`QueueFull`; call `ReleaseAll()` or stop the session to handle release events that could
not be queued. To finish submitted events, call `WaitForIdle()` before `Stop()`.

Calling `Stop()` externally waits for shutdown. Calling it from the injection worker's
error callback only requests shutdown, avoiding a wait on the same thread.
Exceptions thrown by callbacks are contained. A session can be restarted after it has
fully stopped.

## Other events

| Type | Key parameters |
| --- | --- |
| `Button` | `MouseButton::Left`, `Right`, `Middle`, `Back`, `Forward`, and `down` |
| `Pointer` | `Desktop` coordinates, `RelativeMotion` deltas, or `Normalized` coordinates |
| `Scroll` | Positive `x` scrolls right and positive `y` scrolls up; one unit is one wheel notch |
| `Touch` | `Down`, `Move`, `Up`, `Cancel`; contact IDs 0–9; normalized coordinates |

Desktop and relative coordinates are signed, allowing negative coordinates across
multiple displays. Normalized coordinates range from `[0, 65535]`. `MapPoint()` converts
positions in a preview window to normalized coordinates, accounting for letterboxing
and rotation. When dragging outside the image, pass `clamp=true` to keep the release
position within its boundaries.

## Monitor input

```cpp
#include <breinput/monitor.hpp>
#include <iostream>

int main() {
    breinput::Monitor monitor(
        [](const breinput::Event& event) {
            if (const auto* key = std::get_if<breinput::Key>(&event);
                key && key->GetCode() == breinput::KeyCode::A && key->down) {
                std::cout << "A pressed\n";
            }
        },
        [](const breinput::Error& error) {
            std::cerr << error.message << '\n';
        });
    if (auto error = monitor.Start()) {
        std::cerr << error.message << '\n';
        return 1;
    }
    std::cin.get();
    monitor.Stop();
}
```

The default `CaptureMode::Global` applies to Windows, macOS, and Linux X11.
On Wayland, pass `MonitorConfig{.mode = CaptureMode::Controlled}` as the third constructor
argument. `MonitorConfig` still supports designated initialization.

Event callbacks run on a separate delivery thread; native capture threads only queue
events. Synchronize data shared between callbacks and other threads, and keep objects
referenced by callbacks alive until `monitor.Stop()` completes.
Use `GetButton()` to read mouse button names.

## Recording and playback

`Recorder::Record(event)` stores valid events and their monotonic time offsets relative
to the first event. `Snapshot()` returns a thread-safe copy and `Clear()` clears the
recording. You can call `Record()` from a `Monitor` callback.

Start the injector before calling `Playback(input, recorder.Snapshot(), stop_token)`.
Playback submits events on the calling thread according to their timestamps.
A `std::stop_token` can interrupt waits; normal completion or cancellation requests
release of inputs held by that injector. Use a separate injector for playback to avoid
sharing held-key state with other operations. Recordings are currently kept in memory;
no file format is provided.

## Manual example

With `BREINPUT_BUILD_EXAMPLES` enabled, single-configuration builds place the example
at `build/examples/breinput_demo`:

```sh
./build/examples/breinput_demo --monitor
./build/examples/breinput_demo --capture-edge
./build/examples/breinput_demo --move 32768 32768
```

`--monitor` stops when Enter is pressed. For `--capture-edge`, cross the right screen
edge after authorization to activate capture; press Escape or wait 30 seconds to stop.
`--move` takes normalized coordinates in `[0, 65535]`. Multi-configuration builds may
place executables in `build/examples/Release/`; Windows filenames have an `.exe` suffix.
