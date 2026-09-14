# BreInput

[English](../README.md)

C++23 静态库，提供键盘、鼠标和触摸注入、输入监听、录制与回放。
支持 Windows、macOS 和 Linux（X11 / Wayland），具体能力与权限要求因平台而异。

## 构建

[平台依赖](build.zh-CN.md)。

```sh
cmake -S . -B build
cmake --build build --parallel
cmake --install build --prefix install
```

CMake 目标链接 `BreInput::BreInput` 即可。支持通过 `add_subdirectory()` 接入源码，
或通过 `find_package()` 使用安装包。

## 文档

详细指南：

- [构建与接入](build.zh-CN.md)
- [注入、监听与录制](usage.zh-CN.md)
- [键鼠映射与接口兼容性](key-mapping.zh-CN.md)
- [目录结构与开发](development.zh-CN.md)

## 许可证

本仓库尚未设置开源许可证。
