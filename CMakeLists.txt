cmake_minimum_required(VERSION 3.16)
project(memory_visualizer LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Shared object that gets LD_PRELOADed
add_library(wrap SHARED
    src/backend/heap_analyzer.c
    src/backend/trace_analyzer.c
    src/backend/stack_analyzer.c
    src/backend/logger.c
    src/backend/init.c
)

add_subdirectory(external/cJSON)

target_link_libraries(wrap PRIVATE dl cjson)
target_include_directories(wrap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
    tracer_state.h
)


# Prevent the wrapper itself from being instrumented
target_compile_options(wrap PRIVATE
    -fPIC
)

set_target_properties(wrap PROPERTIES
    OUTPUT_NAME "wrap"
    PREFIX ""
)




# Runner executable
add_executable(memvis
    src/runner/runner.c
)

# Central place for all flags required by traced executables
add_library(instrumentation_flags INTERFACE)

target_compile_options(instrumentation_flags INTERFACE
    -finstrument-functions
    -fno-omit-frame-pointer
    -fno-inline
    -g3
    -O0
)

target_link_options(instrumentation_flags INTERFACE
    -rdynamic
)

# Example traced program
add_executable(testprog1
    tests/testprog.c
)

target_link_libraries(testprog1 PRIVATE instrumentation_flags)


add_executable(testprog2
    tests/fun_trace2.c
)


target_link_libraries(testprog2 PRIVATE instrumentation_flags)

add_executable(leaky
    tests/leaky.c
)


target_link_libraries(leaky PRIVATE instrumentation_flags)


add_executable(snake
    tests/snake.c
)

target_link_libraries(snake PRIVATE instrumentation_flags)
