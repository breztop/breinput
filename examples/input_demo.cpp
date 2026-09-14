#include <atomic>
#include <breinput/device.hpp>
#include <breinput/monitor.hpp>
#include <iostream>
#include <syncstream>
#include <thread>
int main(int argc, char** argv) {
    auto error = [](const breinput::Error& value) {
        std::osyncstream(std::cerr) << value.message << '\n';
    };
    if (argc == 2 &&
        (std::string(argv[1]) == "--monitor" || std::string(argv[1]) == "--capture-edge")) {
        breinput::MonitorConfig config;
        if (std::string(argv[1]) == "--capture-edge") {
            config.mode = breinput::CaptureMode::Controlled;
        }
        std::atomic<bool> stop = false;
        breinput::Monitor monitor(
            [&](const breinput::Event& event) {
                if (auto* key = std::get_if<breinput::Key>(&event);
                    key && key->GetCode() == breinput::KeyCode::Escape && key->down) {
                    stop = true;
                }
                std::osyncstream(std::cout) << "event kind=" << event.index() << '\n';
            },
            error, config);
        if (auto result = monitor.Start()) {
            error(result);
            return 1;
        }
        if (config.mode == breinput::CaptureMode::Controlled) {
            std::cout
                << "Cross the right screen edge to capture. Escape stops; timeout 30 seconds.\n";
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!stop && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            std::cout << "Monitoring input; press Enter to stop.\n";
            std::cin.get();
        }
        monitor.Stop();
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--move") {
        try {
            breinput::Injector injector({}, error);
            if (auto result = injector.Start()) {
                error(result);
                return 1;
            }
            const auto result = injector.Submit(breinput::Pointer{
                breinput::CoordinateSpace::Normalized, std::stoi(argv[2]), std::stoi(argv[3])});
            if (result) {
                error(result);
                return 1;
            }
            injector.WaitForIdle();
            injector.Stop();
            return 0;
        } catch (const std::exception& exception) {
            std::cerr << exception.what() << '\n';
            return 1;
        }
    }
    std::cout << "Usage: breinput_demo --monitor | --capture-edge | --move X Y\nX/Y: normalized "
                 "screen coordinates, 0..65535.\n";
}
