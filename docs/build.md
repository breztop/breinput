# Build and integration

[中文](build.zh-CN.md) · [Home](../README.md)

## Requirements and platform capabilities

Requires CMake 3.16+ and a compiler and standard library with C++23 support.
The library uses standard facilities such as threads and `std::stop_token`;
the manual example also uses `std::syncstream`. The project neither downloads nor
bundles third-party code. Builds require the system development libraries below,
and enabling tests additionally requires Boost headers.

| Platform | Build dependencies | Injection | Monitoring |
| --- | --- | --- | --- |
| Windows | System User32 | Keyboard and mouse; touch depends on system support | Global keyboard and mouse hooks |
| macOS | System ApplicationServices | Keyboard and mouse, with Accessibility permission | Event monitoring, with system permission |
| Linux X11 | pkg-config, X11, XTest/XRecord, GLib GIO, libei 1.2+ headers | Keyboard and mouse | XRecord keyboard and mouse monitoring |
| Linux Wayland | Same as Linux X11 | Input authorized through RemoteDesktop Portal | Controlled capture through InputCapture Portal |

Windows injection may be restricted by target process permissions and desktop restrictions.
The X11 and macOS backends do not support touch injection. On Wayland, available event
types depend on Portal authorization and device capabilities; query `GetCapabilities()`
after startup.

## Build, test, and install

Run from the repository root:

```sh
cmake -S . -B build -DBREINPUT_BUILD_TESTS=ON -DBREINPUT_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

| CMake variable | Default | Purpose |
| --- | --- | --- |
| `BREINPUT_BUILD_TESTS` | `OFF` | Build automated tests; requires Boost headers |
| `BREINPUT_BUILD_EXAMPLES` | `OFF` | Build the manual example, `breinput_demo` |
| `BREINPUT_LIBEI_INCLUDE_DIR` | Automatically located | Directory containing `libei.h` on Linux |

For multi-configuration generators such as Visual Studio, add `--config Release`
to the build and install commands, and `-C Release` to the test command.
When using Clang on macOS, the current CMake configuration passes
`-fexperimental-library`; the selected toolchain must support this option.

## Integrate from source

After defining your application target, add:

```cmake
add_subdirectory(path/to/breinput breinput-build)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

Include public headers as `<breinput/...>`. The target propagates include paths,
C++ standard requirements, and link dependencies.

## Use an installed package

The installation includes the static library, public headers, and CMake package
configuration. In the consuming project, use:

```cmake
find_package(BreInput CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

When configuring the consuming project, set `CMAKE_PREFIX_PATH` to the installation
directory, for example:

```sh
cmake -S path/to/your-app -B app-build -DCMAKE_PREFIX_PATH=/absolute/path/to/breinput/install
```

Consumers of the installed Linux package still need X11/XTest and GIO development
dependencies for linking, but do not need libei headers or a libei link library.
Public headers do not expose GIO or libei types.

## Linux and Wayland

Linux builds compile both the X11 and Wayland backends. For libei headers outside
system locations, set `-DBREINPUT_LIBEI_INCLUDE_DIR=/path/to/libei-1.0`.

libei is loaded at runtime: the first use of a relevant feature loads `libei.so.1`
and checks the required symbols. If the library or required symbols are missing,
injection can continue through Portal Notify, while controlled capture returns an
error. X11 does not load libei. EIS is preferred when the Portal supports it and the
library is available; older Portals without `ConnectToEIS` use Notify. Other errors
encountered during EIS connection are returned normally; not every failure triggers
automatic fallback.

Injection selects the Portal backend when `XDG_SESSION_TYPE=wayland` or
`WAYLAND_DISPLAY` is set. Monitoring is selected through `MonitorConfig::mode`:
`Global` uses XRecord and `Controlled` uses InputCapture. Use controlled mode on
Wayland. It requires the desktop to implement InputCapture Portal, with the compositor
activating capture after authorization, currently at the rightmost screen edge.
It does not provide passive global monitoring.

Portal authorization waits support cancellation and have a time limit. Normalized
positions refer to the screen selected in the Portal; EIS prefers matching regions
by the screen's `mapping_id`. The X11 backend follows the Xorg evdev keycode convention.
