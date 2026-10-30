cmake_minimum_required(VERSION 3.24)
project(MagnumEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All executables and DLLs end up in <build>/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(cmake/CompilerFlags.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(third_party/glad)
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(tools/mcp)
# The MagnumDemo sample project game module (the tests load it). Other projects are built by the editor.
add_subdirectory(Projects/MagnumDemo/Game ${CMAKE_BINARY_DIR}/MagnumDemoGame)

enable_testing()
add_subdirectory(tests)
