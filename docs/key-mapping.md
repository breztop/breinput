# Key mapping and API compatibility

[中文](key-mapping.zh-CN.md) · [Home](../README.md) · [Usage guide](usage.md)

## Use named keys and buttons

`KeyCode` is defined in [key_code.hpp](../src/breinput/key_code.hpp) and is available
when including `device.hpp` or `event.hpp`. Injection and monitoring share the same
names for letters, main keyboard digits `Digit0`–`Digit9`, punctuation, arrow keys,
F1–F24, keypad keys, and left and right modifiers.

```cpp
#include <breinput/event.hpp>

constexpr breinput::Key press{breinput::KeyCode::A, true};
constexpr breinput::Key release{breinput::KeyCode::A, false};
static_assert(press.GetCode() == breinput::KeyCode::A);

constexpr breinput::Button left{breinput::MouseButton::Left, true};
static_assert(left.GetButton() == breinput::MouseButton::Left);
```

Pass these events to `Submit()` on a started injector. See the [usage guide](usage.md)
for error handling. `MouseButton` also includes `Right`, `Middle`, `Back`, and `Forward`.
The last two represent buttons 4 and 5; their actual actions depend on the target
application.

## HID identifiers

Keycodes follow the Keyboard/Keypad table in the
[USB HID Usage Tables](https://www.usb.org/sites/default/files/hut1_21_0.pdf).
The Page identifies a category, and the Usage ID identifies a particular key within it:

| Field | Value | Meaning |
| --- | --- | --- |
| Usage Page | `0x07` | Keyboard and keypad |
| Usage ID | `0x04` | Physical A key |

`Key` assumes Page `0x07`; its `usage` field stores only the Usage ID.
`ToHidUsage(KeyCode::A)` returns `4`. Callers normally use the enum directly without
looking up numeric codes. The character encoding of `'A'` differs from its HID keycode
and must not be passed as the A key's `usage`.

Names identify physical keys and do not guarantee corresponding characters.
Case, punctuation, AltGr, and input method behavior depend on the target layout and
state. Entering uppercase A typically requires Shift+A; this is a different capability
from directly submitting text.

## Modifiers across platforms

Enum HID values are identical on every platform; backends convert them to native keycodes.

| Unified names (left and right defined separately) | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `LeftMeta` / `RightMeta` | Win | Command ⌘ | Super |
| `LeftAlt` / `RightAlt` | Alt | Option ⌥ | Alt |
| `LeftControl` / `RightControl` | Ctrl | Control ⌃ | Ctrl |
| `LeftShift` / `RightShift` | Shift | Shift ⇧ | Shift |

`LeftWin`, `LeftCommand`, and `LeftSuper` are equal-valued aliases of `LeftMeta`;
the same applies to the right-side names. Sending `Key{KeyCode::LeftWin, true}` on
macOS presses left Command. When left Command is captured, `GetCode()` can also be
compared directly with `KeyCode::LeftWin`. `LeftOption` and `RightOption` alias the
corresponding Alt keys.

Control remains Control on macOS and is not automatically changed to Command.
To express an action such as Copy, choose Ctrl+C or Command+C according to the input
target's system. If sender and target use different systems, use the target's system.
Submit a chord in this order: modifier press, regular key press, regular key release,
modifier release.

## Native conversion and supported keys

[key_mapping.hpp](../src/breinput/key_mapping.hpp) provides:

| Function | Direction | Return value when unmapped |
| --- | --- | --- |
| `WindowsScanCodeFromHid()` | HID / `KeyCode` → Windows scan code | `0` |
| `HidFromWindowsScanCode()` | Windows scan code → HID | `0` |
| `MacKeyCodeFromHid()` | HID / `KeyCode` → macOS keycode | `-1` |
| `HidFromMacKeyCode()` | macOS keycode → HID | `0` |

A Windows scan code with high byte `0xe0` has an extended prefix. On macOS, native
keycode `0` is a valid A key and must not be used to indicate conversion failure.
Linux evdev conversion is handled by internal backends.

An enum entry does not mean every platform implements that key. For example, F13–F24
currently have no native mappings. Unmapped keys report `Unsupported` during actual
injection; `IsValid()` and `GetCapabilities()` do not check individual platform key
mappings. System permissions or devices can also cause injection to fail.

## Source compatibility after adding enums

Numeric construction and existing fields remain available: `Key{4, true}`,
`Button{1, true}`, `key.usage`, and `button.button`. Event keycodes, fields, and platform
mapping semantics remain the same.

There are two source compatibility changes:

1. `Key` and `Button` now have constructors and are no longer aggregates. Replace
   `Key{.usage = 4, .down = true}` with `Key{4, true}` or `Key{KeyCode::A, true}`;
   the same applies to `Button`. Other configuration structures are unaffected.
2. `WindowsScanCodeFromHid()` and `MacKeyCodeFromHid()` now have `KeyCode` overloads.
   Ordinary calls are unchanged, but taking a function address with `auto` is ambiguous;
   specify the function pointer type explicitly.

```cpp
#include <breinput/key_mapping.hpp>

std::uint16_t (*windows_from_raw)(std::uint16_t) = breinput::WindowsScanCodeFromHid;
int (*mac_from_named)(breinput::KeyCode) = breinput::MacKeyCodeFromHid;
```
