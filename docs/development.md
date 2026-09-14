# Project structure and development

[中文](development.zh-CN.md) · [Home](../README.md) · [Build and integration](build.md)

## Project structure

```text
src/breinput/                # Common .hpp/.cpp modules, public keycodes, internal mapping table
├── lin/
│   ├── x11/                 # X11 injection and monitoring
│   └── wayland/             # Portal, EIS, and dynamic libei loading
├── mac/                     # macOS injection and monitoring
└── win/                     # Windows injection and monitoring
test/                        # Common tests and build configuration
├── lin/                     # Linux-specific tests; fixtures/ holds test shared libraries
└── package/                 # Independent installed-package consumer test
examples/                    # Manual example and build configuration
cmake/                       # CMake package configuration template
docs/                        # English and Chinese documentation
```

Headers and implementations live in the same directory, such as `device.hpp` and
`device.cpp`. Add common functionality to `src/breinput/` and platform code and its
headers to `lin/`, `mac/`, or `win/`.

## Maintaining APIs and build configuration

Register source files in the root [CMakeLists.txt](../CMakeLists.txt).
Only headers in the installation list are public APIs: `device.hpp`, `event.hpp`,
`key_code.hpp`, `key_mapping.hpp`, `monitor.hpp`, and `recording.hpp`.
`key_map.hpp` and internal platform headers are not installed with the SDK;
external projects should use the public APIs.

Implement `InjectionBackend` for a custom injection backend and pass it to the
`Injector` constructor. Implement `CaptureBackend` for a custom capture backend and
pass it to the `Monitor` constructor. Both extension points also support controlled
tests that do not access the real desktop.

## Validation

See [build and integration](build.md) for build and execution commands.
Tests and examples are maintained in [test/CMakeLists.txt](../test/CMakeLists.txt)
and [examples/CMakeLists.txt](../examples/CMakeLists.txt), respectively.

Automated tests cover session lifecycle, queues and input release, recording and
playback, named keys, and keycode conversion for all three platforms. Linux also tests
missing or incomplete libei libraries. The package test installs the SDK inside the
build directory, then builds and runs an independent consumer project.
Automated tests do not inject input into the real desktop.

Keycode conversion tests can check Windows/macOS mapping tables on Linux, but cannot
replace compilation, permission authorization, injection, and monitoring checks on
the target system. See the [usage guide](usage.md) for manual validation entry points.
