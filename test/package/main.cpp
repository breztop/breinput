#include <breinput/device.hpp>
#include <breinput/key_mapping.hpp>
#include <breinput/monitor.hpp>
#include <breinput/recording.hpp>
int main() {
    breinput::Injector input;
    breinput::Monitor monitor([](const breinput::Event&) {
    });
    breinput::Recorder recorder;
    recorder.Record(breinput::Pointer{breinput::CoordinateSpace::Desktop, -1200, 100});
    input.Stop();
    monitor.Stop();
    return recorder.Snapshot().size() == 1 && breinput::HidFromWindowsScanCode(0xe05b) == 227 ? 0
                                                                                              : 1;
}
