cmake_minimum_required(VERSION 3.16)
project(stockparrot VERSION 0.2 LANGUAGES CXX)

# ── Release build by default ──────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(
        -O3
        -Wall
        -Wextra
        -march=native
    )
endif()

# ── Target ────────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

add_executable(stockparrot
    src/driver.cpp
    include/stockparrot/chess.hpp
    include/uci/uci.hpp
)

source_group("Header Files" FILES include/stockparrot/chess.hpp)

target_include_directories(stockparrot PRIVATE include)
target_compile_features(stockparrot PRIVATE cxx_std_20)
target_link_libraries(stockparrot PRIVATE Threads::Threads)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS stockparrot RUNTIME DESTINATION bin)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
)