# 注入、监听与录制

[English](usage.md) · [中文首页](README.zh-CN.md) · [构建与接入](build.zh-CN.md) · [键鼠映射](key-mapping.zh-CN.md)

## 注入一次 A 键

以下示例需要在支持的桌面环境中运行，并满足[平台权限要求](build.zh-CN.md)。

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

`KeyCode::A` 表示物理 A 键。最终字符由目标应用、键盘布局、修饰键和输入法状态决定，
库目前不提供直接输入 Unicode 文本的接口。

## 会话与错误处理

| 接口 | 行为 |
| --- | --- |
| `Start()` | 建立后端会话，返回启动错误；构造函数本身不访问输入设备 |
| `Submit(event)` | 检查参数、会话及事件类型，然后加入有界队列 |
| `WaitForIdle()` | 等待当前队列处理完成或会话结束，不代表目标应用已处理输入 |
| `ReleaseAll()` | 清空待处理队列，并排入释放本实例所持输入的请求 |
| `Stop()` | 清空待处理队列，释放本实例所持输入并关闭会话 |
| `GetCapabilities()` | 查询当前会话支持的事件类型 |

`Error` 转为 `true` 表示失败。`Start()` 和 `Submit()` 的直接错误由返回值报告；
工作线程中的实际注入错误由错误回调报告，`Submit()` 成功仅表示事件已接受。
具体键码、坐标模式或设备仍可能不支持，因此不能只依赖能力查询。

默认队列容量为 256，可通过 `DeviceConfig::queue_capacity` 调整。
只合并相邻且坐标模式一致的绝对移动，保留相对位移和按下、释放的顺序。
队列满返回 `QueueFull`；此时应调用 `ReleaseAll()` 或停止会话，避免释放事件未入队。
若希望已经提交的事件完成，应先 `WaitForIdle()` 再 `Stop()`。

外部调用 `Stop()` 会等待关闭完成；注入工作线程的错误回调内调用只请求停止，避免等待自身。
回调抛出的异常被隔离。会话停止完成后可以再次启动。

## 其他事件

| 类型 | 关键参数 |
| --- | --- |
| `Button` | `MouseButton::Left`、`Right`、`Middle`、`Back`、`Forward`，以及 `down` |
| `Pointer` | `Desktop` 桌面坐标、`RelativeMotion` 相对位移或 `Normalized` 归一化坐标 |
| `Scroll` | `x` 向右为正，`y` 向上为正；单位为一个滚轮刻度 |
| `Touch` | `Down`、`Move`、`Up`、`Cancel`，触点编号 0–9，坐标归一化 |

桌面和相对坐标有符号，允许多显示器的负坐标；归一化坐标范围为 `[0, 65535]`。
`MapPoint()` 可将预览窗口中的位置转换为归一化坐标，处理黑边和旋转；
拖出画面时可传入 `clamp=true`，将释放位置限制在边界内。

## 监听输入

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

默认 `CaptureMode::Global` 适用于 Windows、macOS 和 Linux X11。
Wayland 使用 `MonitorConfig{.mode = CaptureMode::Controlled}`，作为构造函数第三个参数传入。
`MonitorConfig` 仍支持指定成员初始化。

事件回调由独立交付线程执行，原生捕获线程只排队。应用需同步回调与其他线程共享的数据，
并让被回调引用的对象至少存活到 `monitor.Stop()` 完成。鼠标按钮通过 `GetButton()` 读取名称。

## 录制与回放

`Recorder::Record(event)` 保存有效事件及相对首个事件的单调时钟偏移；
`Snapshot()` 返回线程安全的副本，`Clear()` 清空录制。可以在 `Monitor` 回调中调用 `Record()`。

回放前由调用方启动注入器，然后调用
`Playback(input, recorder.Snapshot(), stop_token)`。回放在调用线程按时间提交事件，
`std::stop_token` 可中断等待；正常结束或取消会请求释放该注入器持有的输入。
建议为回放单独创建注入器，避免与其他操作共享持键状态。录制目前保存在内存中，不提供文件格式。

## 手工示例

启用 `BREINPUT_BUILD_EXAMPLES` 后，单配置构建的示例位于 `build/examples/breinput_demo`：

```sh
./build/examples/breinput_demo --monitor
./build/examples/breinput_demo --capture-edge
./build/examples/breinput_demo --move 32768 32768
```

`--monitor` 按 Enter 结束；`--capture-edge` 在授权后越过右侧屏幕边缘激活捕获，
按 Escape 或等待 30 秒结束；`--move` 接受 `[0, 65535]` 内的归一化坐标。
多配置构建的可执行文件可能位于 `build/examples/Release/`，Windows 文件名带 `.exe`。
