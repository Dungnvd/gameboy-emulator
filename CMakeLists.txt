cmake_minimum_required(VERSION 3.16)
project(gbemu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)

add_executable(gbemu src/main.cpp)
target_link_libraries(gbemu PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(gbemu PRIVATE /W4)
else()
    target_compile_options(gbemu PRIVATE -Wall -Wextra -Wpedantic)
endif()