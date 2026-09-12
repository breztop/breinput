execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BREINPUT_BINARY}"
    --prefix "${BREINPUT_BINARY}/package-prefix" --config "${BREINPUT_CONFIG}"
    RESULT_VARIABLE result OUTPUT_QUIET)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "BreInput local SDK installation failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${BREINPUT_SOURCE}/test/package"
    -B "${BREINPUT_BINARY}/package-consumer"
    "-DCMAKE_PREFIX_PATH=${BREINPUT_BINARY}/package-prefix"
    "-DCMAKE_CXX_COMPILER=${BREINPUT_COMPILER}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "BreInput independent consumer configuration failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BREINPUT_BINARY}/package-consumer"
    --config "${BREINPUT_CONFIG}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "BreInput independent consumer build failed")
endif()

execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BREINPUT_BINARY}/package-consumer"
    -C "${BREINPUT_CONFIG}" --output-on-failure RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "BreInput independent consumer execution failed")
endif()
