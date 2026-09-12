#include <dlfcn.h>

#include <boost/test/unit_test.hpp>
#include <cstdlib>

#include "breinput/ei_library.hpp"
#include "breinput/monitor.hpp"
using namespace breinput;
namespace {
bool Loaded(const char* path) {
    void* handle = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    if (handle) {
        dlclose(handle);
    }
    return handle != nullptr;
}
}  // namespace
BOOST_AUTO_TEST_CASE(libei_missing_or_incomplete_returns_error_and_unloads) {
    Error error;
    BOOST_CHECK(!detail::EiLibrary::Load(error, "/breinput-nonexistent/libei.so.1"));
    BOOST_CHECK(error.code == ErrorCode::Unavailable);
    BOOST_CHECK(!detail::EiLibrary::Load(error, BREINPUT_BAD_EI));
    BOOST_CHECK(error.code == ErrorCode::Unsupported);
    BOOST_CHECK(error.message.find("ei_configure_name") != std::string::npos);
    BOOST_CHECK(!Loaded(BREINPUT_BAD_EI));
}
BOOST_AUTO_TEST_CASE(libei_runtime_load_and_shared_lifetime) {
    const char* path = std::getenv("BREINPUT_TEST_LIBEI_PATH");
    const bool explicit_path = path != nullptr;
    if (!path) {
        path = "libei.so.1";
    }
    BOOST_REQUIRE(!Loaded(path));
    Error error;
    auto api = detail::EiLibrary::Load(error, path);
    if (!api && !explicit_path) {
        BOOST_TEST_MESSAGE("No usable libei runtime; available-library test skipped");
        return;
    }
    BOOST_REQUIRE_MESSAGE(api, error.message);
    auto retained = api;
    auto* context = api->ei_new_sender(nullptr);
    BOOST_REQUIRE(context);
    api.reset();
    BOOST_CHECK(Loaded(path));
    retained->ei_unref(context);
    retained.reset();
    BOOST_CHECK(!Loaded(path));
    BOOST_CHECK(detail::EiLibrary::Load(error, path));
}
BOOST_AUTO_TEST_CASE(libei_is_lazy_and_missing_capture_fails_before_portal) {
    if (!std::getenv("BREINPUT_TEST_EXPECT_NO_LIBEI")) {
        return;
    }
    BOOST_REQUIRE(!Loaded("libei.so.1"));
    Injector input;
    MonitorConfig config;
    config.mode = CaptureMode::Controlled;
    Monitor monitor(
        [](const Event&) {
        },
        {}, config);
    BOOST_CHECK(!Loaded("libei.so.1"));
    const auto error = monitor.Start();
    BOOST_CHECK(error.code == ErrorCode::Unavailable);
    BOOST_CHECK(error.message.find("libei.so.1") != std::string::npos);
    monitor.Stop();
    monitor.Stop();
    BOOST_CHECK(monitor.Start().code == ErrorCode::Unavailable);
    monitor.Stop();
}
