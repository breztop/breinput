find_package(Boost REQUIRED)

add_executable(breinput_test test_input.cpp test_key_mapping.cpp)
target_link_libraries(breinput_test PRIVATE BreInput::BreInput)
target_include_directories(breinput_test PRIVATE ${Boost_INCLUDE_DIRS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(breinput_incomplete_ei SHARED lin/fixtures/incomplete_ei.cpp)
    add_library(breinput_without_ei SHARED lin/fixtures/without_libei.cpp)
    target_link_libraries(breinput_without_ei PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME breinput_without_libei COMMAND ${CMAKE_COMMAND} -E env
        LD_PRELOAD=$<TARGET_FILE:breinput_without_ei>
        BREINPUT_TEST_EXPECT_NO_LIBEI=1
        $<TARGET_FILE:breinput_test>
        --run_test=libei_is_lazy_and_missing_capture_fails_before_portal
    )
    set_tests_properties(breinput_without_libei PROPERTIES TIMEOUT 30)
    target_sources(breinput_test PRIVATE lin/test_ei_library.cpp)
    target_include_directories(breinput_test PRIVATE
        ${BREINPUT_LIBEI_INCLUDE_DIR}
    )
    target_compile_definitions(breinput_test PRIVATE
        BREINPUT_BAD_EI="$<TARGET_FILE:breinput_incomplete_ei>"
    )
    add_dependencies(breinput_test breinput_incomplete_ei)
endif()

add_test(NAME breinput_test COMMAND breinput_test)
set_tests_properties(breinput_test PROPERTIES TIMEOUT 30)
add_test(NAME breinput_package_consumer COMMAND ${CMAKE_COMMAND}
    -DBREINPUT_BINARY=${BreInput_BINARY_DIR}
    -DBREINPUT_SOURCE=${BreInput_SOURCE_DIR}
    -DBREINPUT_COMPILER=${CMAKE_CXX_COMPILER}
    -DBREINPUT_CONFIG=$<CONFIG>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/package/verify.cmake
)
set_tests_properties(breinput_package_consumer PROPERTIES TIMEOUT 90)
