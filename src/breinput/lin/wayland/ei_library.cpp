#include "ei_library.hpp"

#include <dlfcn.h>
namespace breinput::detail {
EiLibrary::Shared EiLibrary::Load(Error& error, const char* path) {
    error = {};
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* reason = dlerror();
        error = {ErrorCode::Unavailable, reason ? reason : "Cannot load libei.so.1"};
        return {};
    }
    auto api = std::shared_ptr<EiLibrary>(new EiLibrary(handle));
#define BREINPUT_EI_FUNCTION(name)                                                 \
    api->name = reinterpret_cast<decltype(api->name)>(dlsym(handle, #name));       \
    if (!api->name) {                                                              \
        error = {ErrorCode::Unsupported, "libei missing required symbol: " #name}; \
        return {};                                                                 \
    }
#include "ei_functions.inc"
#undef BREINPUT_EI_FUNCTION
    return api;
}
EiLibrary::~EiLibrary() { dlclose(handle_); }
}  // namespace breinput::detail
