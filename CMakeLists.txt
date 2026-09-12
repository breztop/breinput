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
target_include_directories(breinput PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(breinput PRIVATE Threads::Threads)
if(WIN32)
    target_sources(breinput PRIVATE src/breinput/backend_win.cpp src/breinput/monitor_win.cpp)
    target_compile_definitions(breinput PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(breinput PRIVATE user32)
elseif(APPLE)
    target_sources(breinput PRIVATE src/breinput/backend_mac.cpp src/breinput/monitor_mac.cpp)
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
        src/breinput/ei_library.cpp
        src/breinput/eis_sender.cpp
        src/breinput/monitor_portal.cpp
        src/breinput/backend_x11.cpp
        src/breinput/backend_portal.cpp
        src/breinput/monitor_x11.cpp
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
    find_package(Boost REQUIRED)

    add_executable(breinput_test test/test_input.cpp)
    target_link_libraries(breinput_test PRIVATE BreInput::BreInput)
    target_include_directories(breinput_test PRIVATE ${Boost_INCLUDE_DIRS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(breinput_incomplete_ei SHARED test/incomplete_ei.cpp)
        add_library(breinput_without_ei SHARED test/without_libei.cpp)
        target_link_libraries(breinput_without_ei PRIVATE ${CMAKE_DL_LIBS})
        add_test(NAME breinput_without_libei COMMAND ${CMAKE_COMMAND} -E env
            LD_PRELOAD=$<TARGET_FILE:breinput_without_ei>
            BREINPUT_TEST_EXPECT_NO_LIBEI=1
            $<TARGET_FILE:breinput_test>
            --run_test=libei_is_lazy_and_missing_capture_fails_before_portal
        )
        set_tests_properties(breinput_without_libei PROPERTIES TIMEOUT 30)
        target_sources(breinput_test PRIVATE test/test_ei_library.cpp)
        target_include_directories(breinput_test PRIVATE ${BREINPUT_LIBEI_INCLUDE_DIR})
        target_compile_definitions(breinput_test PRIVATE
            BREINPUT_BAD_EI="$<TARGET_FILE:breinput_incomplete_ei>"
        )
        add_dependencies(breinput_test breinput_incomplete_ei)
    endif()
    add_test(NAME breinput_test COMMAND breinput_test)
    set_tests_properties(breinput_test PROPERTIES TIMEOUT 30)
    add_test(NAME breinput_package_consumer COMMAND ${CMAKE_COMMAND}
        -DBREINPUT_BINARY=${CMAKE_CURRENT_BINARY_DIR}
        -DBREINPUT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -DBREINPUT_COMPILER=${CMAKE_CXX_COMPILER}
        -DBREINPUT_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/package/verify.cmake
    )
    set_tests_properties(breinput_package_consumer PROPERTIES TIMEOUT 90)
endif()

if(BREINPUT_BUILD_EXAMPLES)
    add_executable(breinput_demo examples/input_demo.cpp)
    target_link_libraries(breinput_demo PRIVATE BreInput::BreInput)
endif()
