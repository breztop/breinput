cmake_minimum_required(VERSION 3.16)
project(BreInput VERSION 0.1.0 LANGUAGES CXX)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(BREINPUT_BUILD_EXAMPLES "Build the manual input example" OFF)
option(BREINPUT_BUILD_TESTS "Build deterministic BreInput tests" OFF)
find_package(Threads REQUIRED)
add_library(breinput STATIC
    src/breinput/event.cpp
    src/breinput/key_mapping.cpp
    src/breinput/device.cpp
    src/breinput/recording.cpp
    src/breinput/monitor.cpp
)
add_library(BreInput::BreInput ALIAS breinput)
set_target_properties(breinput PROPERTIES
    EXPORT_NAME BreInput
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_compile_features(breinput PUBLIC cxx_std_23)
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(breinput PUBLIC -fexperimental-library)
    target_link_options(breinput PUBLIC -fexperimental-library)
endif()
target_include_directories(breinput
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(breinput PRIVATE Threads::Threads)
if(WIN32)
    target_sources(breinput PRIVATE
        src/breinput/win/backend_win.cpp
        src/breinput/win/monitor_win.cpp
    )
    target_compile_definitions(breinput PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(breinput PRIVATE user32)
elseif(APPLE)
    target_sources(breinput PRIVATE
        src/breinput/mac/backend_mac.cpp
        src/breinput/mac/monitor_mac.cpp
    )
    target_link_libraries(breinput PRIVATE "-framework ApplicationServices")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BREINPUT_X11 REQUIRED IMPORTED_TARGET x11 xtst)
    pkg_check_modules(BREINPUT_GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
    find_path(BREINPUT_LIBEI_INCLUDE_DIR libei.h PATH_SUFFIXES libei-1.0)
    if(NOT BREINPUT_LIBEI_INCLUDE_DIR)
        message(FATAL_ERROR "Linux builds require libei 1.2+ headers; set BREINPUT_LIBEI_INCLUDE_DIR")
    endif()
    target_include_directories(breinput PRIVATE ${BREINPUT_LIBEI_INCLUDE_DIR})
    target_link_libraries(breinput PRIVATE ${CMAKE_DL_LIBS})
    target_sources(breinput PRIVATE
        src/breinput/lin/x11/backend_x11.cpp
        src/breinput/lin/x11/monitor_x11.cpp
        src/breinput/lin/wayland/backend_portal.cpp
        src/breinput/lin/wayland/monitor_portal.cpp
        src/breinput/lin/wayland/ei_library.cpp
        src/breinput/lin/wayland/eis_sender.cpp
    )
    target_link_libraries(breinput PRIVATE PkgConfig::BREINPUT_X11 PkgConfig::BREINPUT_GIO)
else()
    message(FATAL_ERROR "BreInput supports Windows, Linux and macOS")
endif()
install(TARGETS breinput
    EXPORT BreInputTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    src/breinput/monitor.hpp
    src/breinput/key_mapping.hpp
    src/breinput/key_code.hpp
    src/breinput/event.hpp
    src/breinput/device.hpp
    src/breinput/recording.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/breinput
)
install(EXPORT BreInputTargets
    NAMESPACE BreInput::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BreInput
)
configure_package_config_file(
    cmake/BreInputConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BreInputConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BreInput
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/BreInputConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BreInputConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/BreInputConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BreInput
)
if(BREINPUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(BREINPUT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
