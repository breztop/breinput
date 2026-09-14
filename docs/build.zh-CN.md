# 构建与接入

[English](build.md) · [中文首页](README.zh-CN.md)

## 环境与平台能力

需要 CMake 3.16+，以及支持 C++23 的编译器和标准库。库使用线程、`std::stop_token`
等标准设施；手工示例还使用 `std::syncstream`。项目不下载或内置第三方代码，
构建依赖下表中的系统开发库，启用测试时另需 Boost 头文件。

| 平台 | 构建依赖 | 注入 | 监听 |
| --- | --- | --- | --- |
| Windows | 系统 User32 | 键鼠；触摸取决于系统支持 | 全局键鼠钩子 |
| macOS | 系统 ApplicationServices | 键鼠，需要辅助功能权限 | 事件监听，需要系统授权 |
| Linux X11 | pkg-config、X11、XTest/XRecord、GLib GIO、libei 1.2+ 头文件 | 键鼠 | XRecord 键鼠 |
| Linux Wayland | 与 Linux X11 相同 | RemoteDesktop Portal 授权后的输入 | InputCapture Portal 的受控捕获 |

Windows 注入可能受目标进程权限和桌面限制影响。X11 和 macOS 后端不支持触摸注入。
Wayland 的具体事件类型取决于 Portal 授权及设备能力，启动后通过 `GetCapabilities()` 查询。

## 构建、测试与安装

在仓库根目录执行：

```sh
cmake -S . -B build -DBREINPUT_BUILD_TESTS=ON -DBREINPUT_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

| CMake 变量 | 默认值 | 用途 |
| --- | --- | --- |
| `BREINPUT_BUILD_TESTS` | `OFF` | 构建自动测试，需要 Boost 头文件 |
| `BREINPUT_BUILD_EXAMPLES` | `OFF` | 构建手工示例 `breinput_demo` |
| `BREINPUT_LIBEI_INCLUDE_DIR` | 自动查找 | Linux 下 `libei.h` 所在目录 |

多配置生成器（例如 Visual Studio）需在构建和安装命令中添加 `--config Release`，
测试命令添加 `-C Release`。macOS 使用 Clang 时，当前 CMake 配置会传递
`-fexperimental-library`，所选工具链需支持此选项。

## 从源码接入

在调用方定义好目标后，添加：

```cmake
add_subdirectory(path/to/breinput breinput-build)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

公开头文件统一通过 `<breinput/...>` 引用。头文件路径、C++ 标准要求及链接依赖由目标传递。

## 使用安装包

安装内容包含静态库、公开头文件和 CMake 包配置。调用方使用：

```cmake
find_package(BreInput CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

配置调用方时，将 `CMAKE_PREFIX_PATH` 指向安装目录，例如：

```sh
cmake -S path/to/your-app -B app-build -DCMAKE_PREFIX_PATH=/absolute/path/to/breinput/install
```

Linux 安装包的调用方仍需 X11/XTest 和 GIO 开发依赖以完成链接，
但无需 libei 头文件或链接库。公开头文件不暴露 GIO 或 libei 类型。

## Linux 与 Wayland

Linux 构建会同时编译 X11 和 Wayland 后端。非系统位置的 libei 头文件可以通过
`-DBREINPUT_LIBEI_INCLUDE_DIR=/path/to/libei-1.0` 指定。

libei 采用运行时加载：首次使用相关功能时加载 `libei.so.1` 并检查所需符号。
缺库或符号不全时，注入可继续使用 Portal Notify，受控捕获返回错误；X11 不加载 libei。
Portal 支持 EIS 且库可用时优先连接 EIS，旧版 Portal 未提供 `ConnectToEIS` 时使用 Notify。
已经进入 EIS 连接流程的其他错误会正常返回，不保证所有失败都自动回退。

注入根据 `XDG_SESSION_TYPE=wayland` 或 `WAYLAND_DISPLAY` 选择 Portal 后端。
监听通过 `MonitorConfig::mode` 选择：`Global` 使用 XRecord，`Controlled` 使用 InputCapture。
Wayland 下应使用受控模式，它要求桌面实现 InputCapture Portal，并在授权后由合成器激活捕获，
当前使用屏幕最右侧边缘；它不提供被动全局监听。

Portal 授权等待支持取消并有时间上限。归一化位置对应 Portal 中选择的屏幕，
EIS 优先通过屏幕的 `mapping_id` 匹配区域。X11 后端使用 Xorg evdev 键码约定。
