# BreInput

[中文](docs/README.zh-CN.md)

A C++23 static library for keyboard, mouse, and touch injection, input monitoring,
recording, and playback. Supports Windows, macOS, and Linux (X11 / Wayland).
Platform capabilities and permissions vary.

## Build

[platform dependencies](docs/build.md).

```sh
cmake -S . -B build
cmake --build build --parallel
cmake --install build --prefix install
```

Link your CMake target against `BreInput::BreInput`. Both source integration via
`add_subdirectory()` and installed packages via `find_package()` are supported.

## Documentation

Detailed guides:

- [Build and integration](docs/build.md)
- [Injection, monitoring, and recording](docs/usage.md)
- [Key mapping and API compatibility](docs/key-mapping.md)
- [Project structure and development](docs/development.md)

## License

No open-source license has been specified for this repository.
