cmake_minimum_required(VERSION 3.20)
project(citsy VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CITSY_BUILD_TESTS            "Build unit tests"                 ON)
option(CITSY_BUILD_EXAMPLES         "Build example programs"           ON)
option(CITSY_BUILD_RAYLIB_BACKEND   "Build the raylib reference player" OFF)

# ---------------------------------------------------------------------------
# Core library: citsy
# ---------------------------------------------------------------------------
add_library(citsy STATIC
    src/parser/parser.cpp
    src/engine/engine.cpp
    src/dialog/linear.cpp
    src/dialog/script.cpp
    src/render/compose.cpp
)

target_include_directories(citsy
    PUBLIC
        # Public headers: #include <citsy/...>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Internal headers: src/model/game.hpp, src/parser/parser.hpp, …
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(citsy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# citsy must have ZERO link deps on platform/media libraries.
# Add only header-only or pure-logic dependencies here.

# ---------------------------------------------------------------------------
# Mock backend (header-only interface, referenced from tests/examples)
# ---------------------------------------------------------------------------
# backends/mock/mock_host.hpp is header-only; no separate target needed.
# Tests and examples include it via the source tree include path.

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CITSY_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 v3
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(citsy_tests
        tests/unit/test_parser.cpp
        tests/unit/test_engine.cpp
        tests/unit/test_dialog.cpp
        tests/unit/test_simulation.cpp
    )

    target_link_libraries(citsy_tests
        PRIVATE
            citsy
            Catch2::Catch2WithMain
    )

    target_include_directories(citsy_tests
        PRIVATE
            # Allow includes like: #include "src/parser/parser.hpp"
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Allow includes like: #include "backends/mock/mock_host.hpp"
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    include(Catch)
    catch_discover_tests(citsy_tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(CITSY_BUILD_EXAMPLES)
    add_executable(minimal_example
        examples/minimal/main.cpp
    )

    target_link_libraries(minimal_example PRIVATE citsy)

    target_include_directories(minimal_example
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ---------------------------------------------------------------------------
# raylib backend (optional)
# ---------------------------------------------------------------------------
if(CITSY_BUILD_RAYLIB_BACKEND)
    add_subdirectory(backends/raylib)
endif()
