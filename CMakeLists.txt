cmake_minimum_required(VERSION 3.24)
project(gbemu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5.zip)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

function(gbemu_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Emulator core: plain C++, no SDL/ImGui
add_library(gbcore STATIC
    src/core/registers.cpp
    src/core/mmu.cpp
)

target_include_directories(gbcore PUBLIC src)
gbemu_set_warnings(gbcore)

# Desktop frontend
add_executable(gbemu src/main.cpp)
target_link_libraries(gbemu PRIVATE gbcore imgui)
gbemu_set_warnings(gbemu)

# Unit tests
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(gbcore_tests
    tests/registers_test.cpp
    tests/mmu_test.cpp
)
target_link_libraries(gbcore_tests PRIVATE gbcore GTest::gtest_main)
gbemu_set_warnings(gbcore_tests)

include(GoogleTest)
gtest_discover_tests(gbcore_tests)
