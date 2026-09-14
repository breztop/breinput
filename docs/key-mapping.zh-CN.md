# 键鼠映射与接口兼容性

[English](key-mapping.md) · [中文首页](README.zh-CN.md) · [使用指南](usage.zh-CN.md)

## 按名称操作

`KeyCode` 定义在 [key_code.hpp](../src/breinput/key_code.hpp)，包含 `device.hpp` 或
`event.hpp` 即可使用。注入和监听共用同一套名称，包含字母、主键盘数字
`Digit0`–`Digit9`、标点、方向键、F1–F24、小键盘和左右修饰键。

```cpp
#include <breinput/event.hpp>

constexpr breinput::Key press{breinput::KeyCode::A, true};
constexpr breinput::Key release{breinput::KeyCode::A, false};
static_assert(press.GetCode() == breinput::KeyCode::A);

constexpr breinput::Button left{breinput::MouseButton::Left, true};
static_assert(left.GetButton() == breinput::MouseButton::Left);
```

将上述事件交给已启动注入器的 `Submit()` 即可，错误处理见[使用指南](usage.zh-CN.md)。
`MouseButton` 还包括 `Right`、`Middle`、`Back`、`Forward`；后两者表示第 4、5 个按钮，
其具体动作由目标应用决定。

## HID 编号

键码遵循 [USB HID Usage Tables](https://www.usb.org/sites/default/files/hut1_21_0.pdf)
中的 Keyboard/Keypad 表，Page 是类别，Usage ID 是该类别中的具体按键：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| Usage Page | `0x07` | 键盘与小键盘 |
| Usage ID | `0x04` | 物理 A 键 |

`Key` 默认采用 Page `0x07`，其 `usage` 字段只存 Usage ID。
`ToHidUsage(KeyCode::A)` 返回 `4`，调用方通常直接使用枚举，无需查数字表。
字符 `'A'` 的编码与 HID 键码不同，不能将它作为 A 键的 `usage` 传入。

名称标识物理键，不保证生成同名字符。大小写、标点、AltGr 和输入法行为由目标布局及状态决定。
输入大写 A 通常需要 Shift+A；它与直接提交文本是不同能力。

## 跨平台修饰键

所有枚举的 HID 值在各平台一致，由后端转换为原生键码。

| 统一名称（左右分别定义） | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `LeftMeta` / `RightMeta` | Win | Command ⌘ | Super |
| `LeftAlt` / `RightAlt` | Alt | Option ⌥ | Alt |
| `LeftControl` / `RightControl` | Ctrl | Control ⌃ | Ctrl |
| `LeftShift` / `RightShift` | Shift | Shift ⇧ | Shift |

`LeftWin`、`LeftCommand`、`LeftSuper` 均为 `LeftMeta` 的等值别名，右侧同理。
发送 `Key{KeyCode::LeftWin, true}` 时，macOS 后端会按下左 Command；
监听到左 Command 后，`GetCode()` 也可以直接与 `KeyCode::LeftWin` 比较。
`LeftOption` / `RightOption` 分别是对应 Alt 的别名。

Control 在 macOS 上仍然是 Control，不会自动变成 Command。应用要表达“复制”等操作时，
需根据输入目标系统选择 Ctrl+C 或 Command+C；发送端与目标系统不同时，应以目标系统为准。
组合键按顺序提交修饰键按下、普通键按下、普通键释放、修饰键释放。

## 原生转换与支持范围

[key_mapping.hpp](../src/breinput/key_mapping.hpp) 提供：

| 函数 | 方向 | 无映射时返回 |
| --- | --- | --- |
| `WindowsScanCodeFromHid()` | HID / `KeyCode` → Windows 扫描码 | `0` |
| `HidFromWindowsScanCode()` | Windows 扫描码 → HID | `0` |
| `MacKeyCodeFromHid()` | HID / `KeyCode` → macOS 键码 | `-1` |
| `HidFromMacKeyCode()` | macOS 键码 → HID | `0` |

Windows 扫描码的高字节 `0xe0` 表示扩展前缀。macOS 原生键码 `0` 是有效的 A 键，
不能用它表示转换失败。Linux evdev 转换由内部后端处理。

枚举存在不代表每个平台都实现该键。例如当前 F13–F24 尚未提供原生映射。
未映射的键在实际注入时报告 `Unsupported`；`IsValid()` 和 `GetCapabilities()`
不会逐个验证平台键码映射。系统权限或设备也可能使注入失败。

## 新增枚举后的源码兼容性

原有数字构造和字段继续可用：`Key{4, true}`、`Button{1, true}`、`key.usage`、
`button.button`。事件键码、字段和平台映射语义保持一致。

有两处源码兼容性变化：

1. `Key` 和 `Button` 新增构造函数，不再是聚合类型。原先的
   `Key{.usage = 4, .down = true}` 需改为 `Key{4, true}` 或 `Key{KeyCode::A, true}`；
   `Button` 同理。其他配置结构不受此变化影响。
2. `WindowsScanCodeFromHid()` 和 `MacKeyCodeFromHid()` 新增 `KeyCode` 重载。
   普通调用不变，但用 `auto` 直接获取函数地址会产生歧义，需要明确函数指针类型。

```cpp
#include <breinput/key_mapping.hpp>

std::uint16_t (*windows_from_raw)(std::uint16_t) = breinput::WindowsScanCodeFromHid;
int (*mac_from_named)(breinput::KeyCode) = breinput::MacKeyCodeFromHid;
```
