cmake_minimum_required(VERSION 3.20)
project(acclorite VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACCLORITE_BUILD_TESTS "Build Acclorite tests" ON)

add_library(acclorite_core STATIC
    src/core/query.cpp
    src/core/search_engine.cpp
    src/output/terminal_renderer.cpp
    src/output/json_renderer.cpp
    src/sources/path_source.cpp
)

target_include_directories(acclorite_core PUBLIC include)

target_compile_options(acclorite_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(acclorite src/main.cpp)
target_link_libraries(acclorite PRIVATE acclorite_core)

install(TARGETS acclorite RUNTIME DESTINATION bin)

if(ACCLORITE_BUILD_TESTS)
    enable_testing()

    add_executable(acclorite_tests
        tests/test_main.cpp
    )
    target_link_libraries(acclorite_tests PRIVATE acclorite_core)
    add_test(NAME acclorite_tests COMMAND acclorite_tests)
endif()
