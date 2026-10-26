# Desktop build of the portable firmware parts (no ESP-IDF):
#   buddy_sim   – the UI in a 412×412 window (SDL2), mouse = touch
#   buddy_shot  – renders screens without a window and writes PNG files
#   tests       – host unit tests (doctest), run with ctest
#
#   cmake -S firmware/sim -B build/fw-sim -G Ninja && cmake --build build/fw-sim
#   ctest --test-dir build/fw-sim --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(deskbuddy_sim C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# LVGL, same version as the firmware (components/*/idf_component.yml)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

FetchContent_Declare(ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ArduinoJson)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest)

add_subdirectory(../components/app_core app_core)
add_subdirectory(../components/ui ui)

# Headless display + PNG writer, shared by buddy_shot and the tests.
find_package(ZLIB REQUIRED)
add_library(simcore STATIC headless.cpp png.cpp)
target_include_directories(simcore PUBLIC .)
target_link_libraries(simcore PUBLIC lvgl ZLIB::ZLIB)

add_executable(buddy_shot shot.cpp)
target_link_libraries(buddy_shot PRIVATE ui simcore)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(buddy_sim main.cpp)
    target_link_libraries(buddy_sim PRIVATE ui SDL2::SDL2)
    target_compile_definitions(lvgl PUBLIC LV_USE_SDL=1)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
else()
    message(STATUS "SDL2 not found: building without buddy_sim (window)")
endif()

enable_testing()
add_subdirectory(../test test)
