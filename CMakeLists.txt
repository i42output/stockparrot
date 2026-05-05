cmake_minimum_required(VERSION 3.16)
project(stockparrot VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
        -march=native        # enables popcnt / bmi on the host CPU
    )
endif()

# ── Target ────────────────────────────────────────────────────────────────────
add_executable(stockparrot src/engine.cpp)
target_compile_features(stockparrot PRIVATE cxx_std_20)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS stockparrot RUNTIME DESTINATION bin)
