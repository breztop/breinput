#pragma once
#include <cstdint>
namespace breinput {
// 返回 0 表示映射不存在；Windows 扫描码高字节 0xe0 表示扩展前缀。
std::uint16_t HidFromWindowsScanCode(std::uint16_t scan_code);
std::uint16_t WindowsScanCodeFromHid(std::uint16_t usage);
std::uint16_t HidFromMacKeyCode(std::uint16_t code);
int MacKeyCodeFromHid(std::uint16_t usage);
}  // namespace breinput
