#include <boost/test/unit_test.hpp>
#include <breinput/event.hpp>
#include <breinput/key_map.hpp>
#include <breinput/key_mapping.hpp>

using namespace breinput;

BOOST_AUTO_TEST_CASE(named_keys_map_to_native_codes_and_back) {
    struct Mapping {
        KeyCode key;
        std::uint16_t windows;
        int mac;
        std::uint16_t evdev;
    };
    // Native scan codes / virtual key codes, independent of the HID enum values.
    constexpr Mapping mappings[] = {
        {KeyCode::A, 0x001e, 0x00, 30},
        {KeyCode::Z, 0x002c, 0x06, 44},
        {KeyCode::Digit1, 0x0002, 0x12, 2},
        {KeyCode::Digit0, 0x000b, 0x1d, 11},
        {KeyCode::Enter, 0x001c, 0x24, 28},
        {KeyCode::Escape, 0x0001, 0x35, 1},
        {KeyCode::Backspace, 0x000e, 0x33, 14},
        {KeyCode::Space, 0x0039, 0x31, 57},
        {KeyCode::Left, 0xe04b, 0x7b, 105},
        {KeyCode::KeypadEnter, 0xe01c, 0x4c, 96},
        {KeyCode::F1, 0x003b, 0x7a, 59},
        {KeyCode::F12, 0x0058, 0x6f, 88},
        {KeyCode::LeftControl, 0x001d, 0x3b, 29},
        {KeyCode::LeftShift, 0x002a, 0x38, 42},
        {KeyCode::LeftOption, 0x0038, 0x3a, 56},
        {KeyCode::LeftWin, 0xe05b, 0x37, 125},
        {KeyCode::RightControl, 0xe01d, 0x3e, 97},
        {KeyCode::RightShift, 0x0036, 0x3c, 54},
        {KeyCode::RightOption, 0xe038, 0x3d, 100},
        {KeyCode::RightWin, 0xe05c, 0x36, 126},
    };
    for (const auto& mapping : mappings) {
        const auto usage = ToHidUsage(mapping.key);
        BOOST_TEST_CONTEXT("HID usage " << usage) {
            BOOST_CHECK_EQUAL(WindowsScanCodeFromHid(mapping.key), mapping.windows);
            BOOST_CHECK_EQUAL(MacKeyCodeFromHid(mapping.key), mapping.mac);
            BOOST_CHECK_EQUAL(detail::kEvdev[usage], mapping.evdev);
            BOOST_CHECK(Key{HidFromWindowsScanCode(mapping.windows)}.GetCode() == mapping.key);
            BOOST_CHECK(Key{HidFromMacKeyCode(mapping.mac)}.GetCode() == mapping.key);
            BOOST_CHECK(Key{detail::FromEvdev(mapping.evdev)}.GetCode() == mapping.key);
        }
    }
    BOOST_CHECK(Key{HidFromWindowsScanCode(0xe05b)}.GetCode() == KeyCode::LeftCommand);
    BOOST_CHECK(Key{HidFromMacKeyCode(0x36)}.GetCode() == KeyCode::RightWin);
    BOOST_CHECK(Key{detail::FromEvdev(125)}.GetCode() == KeyCode::LeftSuper);
}

BOOST_AUTO_TEST_CASE(named_events_preserve_raw_values_and_validation) {
    constexpr Key a{KeyCode::A, true};
    constexpr Key win{KeyCode::LeftWin, true};
    constexpr Key command{KeyCode::LeftCommand, true};
    static_assert(a == Key{4, true});
    static_assert(win == command);
    static_assert(Key{KeyCode::RightWin} == Key{KeyCode::RightCommand});
    static_assert(Key{KeyCode::LeftOption} == Key{KeyCode::LeftAlt});
    static_assert(Key{}.GetCode() == KeyCode::Unknown);
    BOOST_CHECK(IsValid(a));
    BOOST_CHECK(IsValid(win));
    BOOST_CHECK(!IsValid(Key{KeyCode::Unknown}));
    BOOST_CHECK(!IsValid(Key{static_cast<KeyCode>(0xffff)}));

    constexpr MouseButton buttons[] = {MouseButton::Left, MouseButton::Right, MouseButton::Middle,
                                       MouseButton::Back, MouseButton::Forward};
    std::uint8_t raw = 1;
    for (auto button : buttons) {
        const Button named{button, true};
        const Button captured{raw++, true};
        BOOST_CHECK(named == captured);
        BOOST_CHECK(captured.GetButton() == button);
        BOOST_CHECK(IsValid(named));
    }
    static_assert(Button{}.GetButton() == MouseButton::Left);
    BOOST_CHECK(!IsValid(Button{static_cast<MouseButton>(0)}));
    BOOST_CHECK(!IsValid(Button{static_cast<MouseButton>(6)}));
}
