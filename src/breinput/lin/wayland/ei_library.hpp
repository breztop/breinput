#pragma once
#include <libei.h>

#include <breinput/device.hpp>
namespace breinput::detail {
// 后端持有共享所有权，最后一个 EIS 对象释放后才卸载动态库。
class EiLibrary final {
public:
    using Shared = std::shared_ptr<const EiLibrary>;
    static Shared Load(Error& error, const char* path = "libei.so.1");
    ~EiLibrary();
    EiLibrary(const EiLibrary&) = delete;
    EiLibrary& operator=(const EiLibrary&) = delete;
#define BREINPUT_EI_FUNCTION(name) decltype(&::name) name = nullptr;
#include "ei_functions.inc"
#undef BREINPUT_EI_FUNCTION
private:
    explicit EiLibrary(void* handle) : handle_(handle) {}
    void* handle_;
};
}  // namespace breinput::detail
