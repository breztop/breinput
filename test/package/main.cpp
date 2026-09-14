#include <breinput/device.hpp>
#include <breinput/key_code.hpp>
#include <breinput/key_mapping.hpp>
#include <breinput/monitor.hpp>
#include <breinput/recording.hpp>
int main() {
    constexpr breinput::Key win{breinput::KeyCode::LeftWin, true};
    static_assert(win.GetCode() == breinput::KeyCode::LeftCommand);
    constexpr breinput::Button left{breinput::MouseButton::Left, true};
    static_assert(left.GetButton() == breinput::MouseButton::Left);
    breinput::Injector input;
    breinput::Monitor monitor([](const breinput::Event&) {
    });
    breinput::Recorder recorder;
    recorder.Record(breinput::Pointer{breinput::CoordinateSpace::Desktop, -1200, 100});
    input.Stop();
    monitor.Stop();
    return recorder.Snapshot().size() == 1 &&
                   breinput::HidFromWindowsScanCode(0xe05b) == win.usage &&
                   breinput::MacKeyCodeFromHid(win.GetCode()) == 0x37
               ? 0
               : 1;
}
