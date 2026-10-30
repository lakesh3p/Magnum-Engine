# The game module: a DLL the editor loads, hot reloads and runs in Play-In-Editor.
# Every .cpp under src/ is part of it; the editor rebuilds it when a file there changes.
# It talks to the editor only through engine/include/magnum_game_api.h (a C ABI), so it does not link
# against any engine library.
#
# Two ways to build it:
#   * by the editor: scripts/build_game.bat configures this folder on its own (build dir: <project>/Saved/GameBuild)
#     and passes -DMAGNUM_ENGINE_DIR=<engine folder>
#   * as part of the engine build (the MagnumDemo sample, which the tests load)
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MagnumGame LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
if(NOT MAGNUM_ENGINE_DIR)
    set(MAGNUM_ENGINE_DIR ${CMAKE_SOURCE_DIR})
endif()
if(NOT COMMAND magnum_target_defaults)
    include(${MAGNUM_ENGINE_DIR}/cmake/CompilerFlags.cmake)
endif()

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(MagnumGame SHARED ${GAME_SOURCES})
target_include_directories(MagnumGame PRIVATE ${MAGNUM_ENGINE_DIR}/engine/include)
set_target_properties(MagnumGame PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
magnum_target_defaults(MagnumGame)
