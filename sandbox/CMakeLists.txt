cmake_minimum_required(VERSION 3.16)
project(anim_sandbox CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(anim_sandbox
    src/anim_sim.cpp
    src/main.cpp
)
target_include_directories(anim_sandbox PRIVATE src)
target_compile_options(anim_sandbox PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME invariants COMMAND anim_sandbox test)
