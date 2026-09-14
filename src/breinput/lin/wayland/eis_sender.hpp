#pragma once
#include <map>
#include <string>

#include <breinput/device.hpp>
#include "ei_library.hpp"
namespace breinput::detail {
// 所有调用在输入工作线程，EIS 所有权和 Portal 会话所有权分开。
class EisSender final {
public:
    explicit EisSender(EiLibrary::Shared api) : api_(std::move(api)) {}
    ~EisSender();
    Error Start(int fd, std::string mapping_id, std::stop_token stop);
    Error Poll();
    Error Inject(const Event&);
    Capabilities GetCapabilities() const;

private:
    EiLibrary::Shared api_;
    ei* context_ = nullptr;
    std::map<ei_device*, bool> devices_;
    std::map<std::uint8_t, ei_touch*> touches_;
    std::string mapping_id_;
    bool disconnected_ = false;
    std::uint32_t sequence_ = 0;
    ei_device* device(ei_device_capability) const;
    ei_region* region(ei_device*) const;
};
}  // namespace breinput::detail
