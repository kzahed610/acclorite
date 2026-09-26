cmake_minimum_required(VERSION 3.20)
project(acclorite VERSION 0.3.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACCLORITE_BUILD_TESTS "Build Acclorite tests" ON)
option(ACCLORITE_BUILD_RESEARCH "Build experimental M11 research/qualification targets" OFF)

find_package(SQLite3 QUIET)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(Threads REQUIRED)

if(ACCLORITE_BUILD_RESEARCH)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ONNXRUNTIME QUIET IMPORTED_TARGET libonnxruntime)
        pkg_check_modules(UTF8PROC QUIET IMPORTED_TARGET libutf8proc)
    endif()
endif()
include(GNUInstallDirs)

set(ACCLORITE_CURATED_GUIDANCE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/curated-guidance.tsv")
set(ACCLORITE_CURATED_GUIDANCE_BUILD "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite/curated-guidance.tsv")
set(ACCLORITE_ZYPPER_CONFIG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/zypper-readonly.conf")
set(ACCLORITE_ZYPPER_CONFIG_BUILD "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite/zypper-readonly.conf")
set(ACCLORITE_QUOTES_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/quotes.tsv")
set(ACCLORITE_QUOTES_BUILD "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite/quotes.tsv")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/share/acclorite")
configure_file(
    "${ACCLORITE_CURATED_GUIDANCE_SOURCE}"
    "${ACCLORITE_CURATED_GUIDANCE_BUILD}"
    COPYONLY
)
configure_file(
    "${ACCLORITE_ZYPPER_CONFIG_SOURCE}"
    "${ACCLORITE_ZYPPER_CONFIG_BUILD}"
    COPYONLY
)
configure_file(
    "${ACCLORITE_QUOTES_SOURCE}"
    "${ACCLORITE_QUOTES_BUILD}"
    COPYONLY
)

add_library(acclorite_core STATIC
    src/core/query.cpp
    src/core/capabilities.cpp
    src/core/search_engine.cpp
    src/diagnostics/doctor.cpp
    src/output/terminal_renderer.cpp
    src/output/shell_renderer.cpp
    src/output/capabilities_json_renderer.cpp
    src/output/doctor_terminal_renderer.cpp
    src/output/doctor_json_renderer.cpp
    src/output/json_renderer.cpp
    src/query/lexicon.cpp
    src/query/fuzzy.cpp
    src/query/frame.cpp
    src/query/action_target.cpp
    src/query/targets.cpp
    src/query/relevance.cpp
    src/ranking/preference.cpp
    src/ranking/confidence.cpp
    src/guidance/man_provider.cpp
    src/guidance/info_provider.cpp
    src/guidance/tldr_provider.cpp
    src/guidance/curated_provider.cpp
    src/lore/quote_provider.cpp
    src/syntax/man_provider.cpp
    src/syntax/fish_completion_provider.cpp
    src/syntax/grammar_merge.cpp
    src/answer/composer.cpp
    src/answer/binder.cpp
    src/answer/safety_classifier.cpp
    src/sources/path_source.cpp
    src/sources/desktop_source.cpp
    src/sources/man_source.cpp
    src/sources/pacman_source.cpp
    src/sources/apt_source.cpp
    src/sources/dnf_source.cpp
    src/sources/zypper_source.cpp
    src/sources/xbps_source.cpp
    src/sources/pkgfile_enricher.cpp
    src/sources/filesystem_location_source.cpp
    src/system/executable.cpp
    src/system/distro.cpp
    src/system/process.cpp
    src/system/terminal.cpp
)

target_include_directories(acclorite_core PUBLIC include)
target_link_libraries(acclorite_core PUBLIC Threads::Threads)
target_compile_definitions(acclorite_core PRIVATE
    ACCLORITE_CURATED_GUIDANCE_BUILD_FILE="${ACCLORITE_CURATED_GUIDANCE_BUILD}"
    ACCLORITE_CURATED_GUIDANCE_INSTALL_FILE="${CMAKE_INSTALL_FULL_DATADIR}/acclorite/curated-guidance.tsv"
    ACCLORITE_ZYPPER_CONFIG_BUILD_FILE="${ACCLORITE_ZYPPER_CONFIG_BUILD}"
    ACCLORITE_ZYPPER_CONFIG_INSTALL_FILE="${CMAKE_INSTALL_FULL_DATADIR}/acclorite/zypper-readonly.conf"
    ACCLORITE_QUOTES_BUILD_FILE="${ACCLORITE_QUOTES_BUILD}"
    ACCLORITE_QUOTES_INSTALL_FILE="${CMAKE_INSTALL_FULL_DATADIR}/acclorite/quotes.tsv"
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
install(FILES
    "${ACCLORITE_CURATED_GUIDANCE_SOURCE}"
    "${ACCLORITE_ZYPPER_CONFIG_SOURCE}"
    "${ACCLORITE_QUOTES_SOURCE}"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/acclorite
)
install(FILES
    LICENSE
    README.md
    CHANGELOG.md
    docs/install.md
    docs/machine-interface.md
    docs/release.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES docs/acclorite.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    COMMENT "Removing files installed by this build tree"
)

if(ACCLORITE_BUILD_TESTS)
    enable_testing()

    add_executable(acclorite_tests
        tests/test_main.cpp
    )
    target_link_libraries(acclorite_tests PRIVATE acclorite_core)
    add_test(NAME acclorite_tests COMMAND acclorite_tests)

    if(ACCLORITE_BUILD_RESEARCH)
        add_executable(acclorite_m11_injection_runner
            benchmarks/m11_injection_runner.cpp
        )
        target_link_libraries(acclorite_m11_injection_runner PRIVATE acclorite_core)
        add_test(
            NAME acclorite_m11_injection_runner_smoke
            COMMAND acclorite_m11_injection_runner --help
        )

        if(TARGET PkgConfig::ONNXRUNTIME AND TARGET PkgConfig::UTF8PROC)
            add_executable(acclorite_m11_native_onnx_encoder
                benchmarks/m11_native_onnx_encoder.cpp
            )
            target_link_libraries(acclorite_m11_native_onnx_encoder PRIVATE
                PkgConfig::ONNXRUNTIME
                PkgConfig::UTF8PROC
            )
            target_compile_options(acclorite_m11_native_onnx_encoder PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
            )
            add_test(
                NAME acclorite_m11_native_onnx_encoder_smoke
                COMMAND acclorite_m11_native_onnx_encoder --help
            )
        else()
            message(STATUS "M11 native ONNX encoder disabled: libonnxruntime and/or libutf8proc development files not found")
        endif()

        if(Python3_Interpreter_FOUND)
            add_test(
                NAME acclorite_m11_mantext_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_mantext_probe.py
            )
            add_test(
                NAME acclorite_m11_embedding_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_embedding_probe.py
            )
            add_test(
                NAME acclorite_m11_compact_embedding_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_compact_embedding_probe.py
            )
            add_test(
                NAME acclorite_m11_hybrid_fusion_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_hybrid_fusion_probe.py
            )
            add_test(
                NAME acclorite_m11_injection_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_injection_probe.py
            )
            add_test(
                NAME acclorite_m11_ranked_injection_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_ranked_injection_probe.py
            )
            add_test(
                NAME acclorite_m11_confidence_gate_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_confidence_gate_probe.py
            )
            add_test(
                NAME acclorite_m11_agreement_gate_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_agreement_gate_probe.py
            )
            add_test(
                NAME acclorite_m11_semantic_assist_acceptance_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_semantic_assist_acceptance.py
            )
            add_test(
                NAME acclorite_m11_semantic_runtime_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_semantic_runtime_probe.py
            )
            add_test(
                NAME acclorite_m11_native_runtime_probe_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_m11_native_runtime_probe.py
            )
        endif()
    endif()

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME acclorite_benchmark_harness_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/test_harness.py
        )

        add_test(
            NAME acclorite_cli_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_contract.py
                --binary $<TARGET_FILE:acclorite>
        )

        add_test(
            NAME acclorite_schema_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_schema_contract.py
                --binary $<TARGET_FILE:acclorite>
        )

        add_test(
            NAME acclorite_terminal_ux_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terminal_ux.py
                --binary $<TARGET_FILE:acclorite>
        )

        add_test(
            NAME acclorite_install_layout_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_install_layout.py
                --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()
endif()
