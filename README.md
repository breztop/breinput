# BreInput

独立的 C++23 静态输入库，提供键鼠和触摸注入、输入监听、录制与回放。
不依赖 BreFlow、BreConn 或 FFmpeg；不包含网络传输、远程授权或投屏协议。

## 构建与接入

```sh
cmake -S . -B build -DBREINPUT_BUILD_TESTS=ON -DBREINPUT_BUILD_EXAMPLES=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

外部项目可以从源码构建静态库：

```cmake
add_subdirectory(path/to/breinput breinput-build)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

也可以设置 `CMAKE_PREFIX_PATH` 指向安装目录，使用安装好的 SDK：

```cmake
find_package(BreInput CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE BreInput::BreInput)
```

安装包包含静态库、公开头文件和 CMake 导出配置。平台依赖通过目标传递，
调用方不需要手动拼接链接参数。构建过程不下载依赖或安装系统软件。

| 平台 | 构建依赖 | 注入 | 监听 |
| --- | --- | --- | --- |
| Windows | 系统 User32 | 键鼠、系统允许的触摸 | 全局键鼠钩子 |
| macOS | 系统 ApplicationServices | 键鼠，需辅助功能权限 | 事件监听，需系统允许 |
| Linux X11 | X11、XTest/XRecord、GLib GIO、libei 1.2+ 头文件 | 键鼠 | XRecord 键鼠 |
| Linux Wayland | 同上；运行时 libei 可选 | RemoteDesktop Portal 授权后，优先 EIS；旧版 Portal 可用 Notify | libei + InputCapture Portal 的受控捕获 |

Linux 默认编译 EIS 后端，没有启用开关。源码构建需要 libei 1.2+ 头文件，
非系统路径通过 `BREINPUT_LIBEI_INCLUDE_DIR` 指定；不链接 libei，也不自动下载依赖。
首次使用 Wayland 相关功能时尝试加载 `libei.so.1` 并校验必需符号。缺库或符号不齐时，
注入继续使用 Portal Notify，受控捕获返回结构化错误；程序仍能启动，X11 不加载 libei。
安装后的 SDK 消费者无需 libei 头文件或链接库；公开头文件不包含 libei 或 GIO 类型。

Wayland 的受控捕获需要桌面实现 InputCapture Portal，并由桌面在鼠标越过边缘时激活，
不提供绕过系统授权的被动全局监听。RemoteDesktop 和 InputCapture 的支持情况取决于桌面环境。

## 使用

```cpp
#include <breinput/device.hpp>

breinput::Injector input({}, [](const breinput::Error& error) {
    // 运行错误由应用决定：记录、释放输入、停止或稍后重启。
});
if (auto error = input.Start()) {
    // 处理启动失败；此时没有可用的注入会话。
    return;
}
if (auto error = input.Submit(breinput::Key{4, true})) {  // HID 物理 A 键
    input.ReleaseAll();
}
input.WaitForIdle();
input.Stop();  // 自动释放本实例按下的键、按钮和触点。
```

`Injector` 构造不访问设备，`Start()` 返回启动结果；`Submit()` 只负责验证和有界排队，
实际注入失败通过错误回调报告。外部 `Stop()` 等待资源关闭，工作线程错误回调内调用则请求停止，
避免等待自身。回调异常被隔离。停止完成后可以再次启动。

默认队列 256 项，只合并连续绝对移动，保留相对位移和按下/释放的顺序。
队列满返回 `QueueFull`，应用应调用 `ReleaseAll()` 或停止会话。
`GetCapabilities()` 返回当前可用事件类型；具体键码、坐标模式或设备拒绝仍可能返回错误。

事件键码使用 USB HID Keyboard/Keypad Usage Page；桌面和相对坐标有符号，画面坐标归一化到
`[0, 65535]`。`MapPoint()` 处理预览黑边、旋转和拖出画面的释放。触点编号为 0–9。
Windows/macOS 的原生键码转换见 `breinput/key_mapping.hpp`。

Wayland 归一化位置对应 Portal 中选择的屏幕，EIS 优先使用屏幕的 `mapping_id` 匹配区域。
系统授权请求支持取消，并有等待上限。X11 使用 Xorg evdev 键码约定；X11/macOS 不支持触摸注入。

`Monitor` 的事件回调由独立交付线程执行，原生捕获线程只排队。
`Recorder` 保存事件和单调时钟间隔并提供线程安全快照；`Playback` 在调用线程按时间回放，
支持 `std::stop_token` 中断等待。自定义后端可以实现 `InjectionBackend` 或 `CaptureBackend`。

手工示例可运行 `breinput_demo --monitor`、`--capture-edge` 或 `--move X Y`；
不带参数显示用法。自动测试使用可控后端，不会向真实桌面注入输入。

本仓库尚未设置开源许可证。
