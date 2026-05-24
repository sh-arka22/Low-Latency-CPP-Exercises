# Chapter 1 / Repo Ch.2 — build all .cpp files in this directory as one
# gtest executable. Mirrors the Packt 2nd-edition repo layout, plus the
# extra stubs (strict_interfaces, copy_and_swap, raii_lock_guard).
#
# Usage:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/Chapter02-A_Brief_Introduction_to_C++

cmake_minimum_required(VERSION 3.20)
project(Chapter02-A_Brief_Introduction_to_C++ CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -O2 for "abstractions collapse" demo, -g for stepping through, -Wall.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(GTest REQUIRED)

file(GLOB CHAPTER_SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(${PROJECT_NAME} ${CHAPTER_SRC_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE GTest::gtest pthread)
