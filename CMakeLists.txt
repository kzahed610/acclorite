cmake_minimum_required(VERSION 3.20)
project(acclorite VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACCLORITE_BUILD_TESTS "Build Acclorite tests" ON)

find_package(SQLite3 QUIET)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(Threads REQUIRED)
include(GNUInstallDirs)

set(ACCLORITE_CURATED_GUIDANCE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/curated-guidance.tsv")
set(ACCLORITE_CURATED_GUIDANCE_BUILD "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite/curated-guidance.tsv")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite")
configure_file(
    "${ACCLORITE_CURATED_GUIDANCE_SOURCE}"
    "${ACCLORITE_CURATED_GUIDANCE_BUILD}"
    COPYONLY
)

add_library(acclorite_core STATIC
    src/core/query.cpp
    src/core/search_engine.cpp
    src/diagnostics/doctor.cpp
    src/output/terminal_renderer.cpp
    src/output/doctor_terminal_renderer.cpp
    src/output/doctor_json_renderer.cpp
    src/output/json_renderer.cpp
    src/query/lexicon.cpp
    src/query/fuzzy.cpp
    src/query/frame.cpp
    src/query/targets.cpp
    src/query/relevance.cpp
    src/ranking/preference.cpp
    src/ranking/confidence.cpp
    src/guidance/man_provider.cpp
    src/guidance/info_provider.cpp
    src/guidance/tldr_provider.cpp
    src/guidance/curated_provider.cpp
    src/sources/path_source.cpp
    src/sources/desktop_source.cpp
    src/sources/man_source.cpp
    src/sources/pacman_source.cpp
    src/sources/pkgfile_enricher.cpp
    src/sources/filesystem_location_source.cpp
    src/system/executable.cpp
    src/system/process.cpp
)

target_include_directories(acclorite_core PUBLIC include)
target_link_libraries(acclorite_core PUBLIC Threads::Threads)
target_compile_definitions(acclorite_core PRIVATE
    ACCLORITE_CURATED_GUIDANCE_BUILD_FILE="${ACCLORITE_CURATED_GUIDANCE_BUILD}"
    ACCLORITE_CURATED_GUIDANCE_INSTALL_FILE="${CMAKE_INSTALL_FULL_DATADIR}/acclorite/curated-guidance.tsv"
)

if(SQLite3_FOUND)
    target_sources(acclorite_core PRIVATE src/sources/index_source.cpp)
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(acclorite_core PUBLIC SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
        target_link_libraries(acclorite_core PUBLIC SQLite::SQLite3)
    else()
        target_include_directories(acclorite_core PUBLIC ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(acclorite_core PUBLIC ${SQLite3_LIBRARIES})
    endif()
    target_compile_definitions(acclorite_core PUBLIC ACCLORITE_HAS_SQLITE=1)
else()
    message(STATUS "SQLite3 development files not found: building without persistent FTS index")
    target_sources(acclorite_core PRIVATE src/sources/index_source_stub.cpp)
    target_compile_definitions(acclorite_core PUBLIC ACCLORITE_HAS_SQLITE=0)
endif()

target_compile_options(acclorite_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(acclorite src/main.cpp)
target_link_libraries(acclorite PRIVATE acclorite_core)
target_compile_definitions(acclorite PRIVATE ACCLORITE_VERSION="${PROJECT_VERSION}")

if(Python3_Interpreter_FOUND)
    add_custom_target(acclorite_benchmark
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/run.py
            --binary $<TARGET_FILE:acclorite>
        DEPENDS acclorite
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM
    )
endif()

install(TARGETS acclorite RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${ACCLORITE_CURATED_GUIDANCE_SOURCE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/acclorite)

if(ACCLORITE_BUILD_TESTS)
    enable_testing()

    add_executable(acclorite_tests
        tests/test_main.cpp
    )
    target_link_libraries(acclorite_tests PRIVATE acclorite_core)
    add_test(NAME acclorite_tests COMMAND acclorite_tests)

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME acclorite_benchmark_harness_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_harness.py
        )
    endif()
endif()
