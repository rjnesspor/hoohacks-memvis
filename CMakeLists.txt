cmake_minimum_required(VERSION 3.16)
project(memory_visualizer LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Shared object that gets LD_PRELOADed
add_library(wrap SHARED
    src/backend/heap_analyzer.c
)

add_subdirectory(external/cJSON)

target_link_libraries(wrap PRIVATE dl cjson)
target_include_directories(wrap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
)


# Prevent the wrapper itself from being instrumented
target_compile_options(wrap PRIVATE
    -fPIC
)

set_target_properties(wrap PROPERTIES
    OUTPUT_NAME "wrap"
    PREFIX ""
)

add_library(eric_wrap SHARED
    src/backend/function_analyzer.cpp
    src/backend/heap_analyzer.c
)
target_link_libraries(eric_wrap PRIVATE dl cjson)
target_include_directories(eric_wrap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
)

# Prevent the wrapper itself from being instrumented
target_compile_options(eric_wrap PRIVATE
    -fPIC
)



# Runner executable
add_executable(runner
    src/runner/runner.c
)

add_executable(erics_runner
    src/runner/erics_runner.c
)

# Central place for all flags required by traced executables
add_library(instrumentation_flags INTERFACE)

target_compile_options(instrumentation_flags INTERFACE
    -finstrument-functions
    -fno-omit-frame-pointer
    -fno-inline
    -fno-builtin
)

target_link_options(instrumentation_flags INTERFACE
    -rdynamic
)

# Example traced program
add_executable(testprog
    tests/testprog.c
)

target_link_libraries(testprog PRIVATE instrumentation_flags)

add_executable(fun_test
    tests/function_trace_test.c
)


target_link_libraries(fun_test PRIVATE instrumentation_flags)

add_executable(fun_test2
    tests/fun_trace2.c
)

target_compile_options(fun_test2 PRIVATE
    -fno-omit-frame-pointer
    -O0
)

target_link_options(fun_test2 PRIVATE
    -rdynamic
)


