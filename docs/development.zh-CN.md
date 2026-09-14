# 目录结构与开发

[English](development.md) · [中文首页](README.zh-CN.md) · [构建与接入](build.zh-CN.md)

## 目录结构

```text
src/breinput/                # 通用模块的 .hpp 与 .cpp、公开键码和内部映射表
├── lin/
│   ├── x11/                 # X11 注入与监听
│   └── wayland/             # Portal、EIS 与 libei 动态加载
├── mac/                     # macOS 注入与监听
└── win/                     # Windows 注入与监听
test/                        # 通用测试及构建配置
├── lin/                     # Linux 专项测试，fixtures/ 放测试用动态库
└── package/                 # 安装包的独立消费测试
examples/                    # 手工示例及构建配置
cmake/                       # CMake 安装包配置模板
docs/                        # 中英文文档
```

头文件与实现放在同一目录，例如 `device.hpp` 和 `device.cpp`。
新增通用功能放在 `src/breinput/`，平台代码及其头文件放在 `lin/`、`mac/`、`win/` 下。

## 接口与构建维护

源文件在根目录 [CMakeLists.txt](../CMakeLists.txt) 中登记。
只有安装列表中的头文件是公开接口：`device.hpp`、`event.hpp`、`key_code.hpp`、
`key_mapping.hpp`、`monitor.hpp`、`recording.hpp`。
`key_map.hpp` 及平台内部头文件不随 SDK 安装；外部项目应依赖公开接口。

自定义注入后端实现 `InjectionBackend`，通过 `Injector` 构造函数传入；
自定义捕获后端实现 `CaptureBackend`，通过 `Monitor` 构造函数传入。
这两个扩展点也用于不访问真实桌面的可控测试。

## 验证

构建和运行命令见[构建与接入](build.zh-CN.md)。测试与示例分别在
[test/CMakeLists.txt](../test/CMakeLists.txt) 和
[examples/CMakeLists.txt](../examples/CMakeLists.txt) 中维护。

自动测试覆盖会话生命周期、队列与释放、录制回放、命名按键及三平台键码转换，
Linux 另验证缺少或不完整的 libei。安装包测试会在构建目录下安装 SDK，
再构建并运行独立消费项目。自动测试不向真实桌面注入输入。

键码转换测试可以在 Linux 上验证 Windows/macOS 的转换表，但不能替代目标系统上的
实际编译、权限授权、注入和监听验证。手工验证入口见[使用指南](usage.zh-CN.md)。
