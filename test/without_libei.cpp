#include <dlfcn.h>

#include <cstring>
// 仅用于测试：阻止 libei 按需加载，保留 NOLOAD 查询以发现意外的启动依赖。
extern "C" void* dlopen(const char* path, int flags) noexcept {
    if (path && std::strcmp(path, "libei.so.1") == 0 && !(flags & RTLD_NOLOAD)) {
        dlerror();
        return nullptr;
    }
    static auto native = reinterpret_cast<decltype(&dlopen)>(dlsym(RTLD_NEXT, "dlopen"));
    return native(path, flags);
}
