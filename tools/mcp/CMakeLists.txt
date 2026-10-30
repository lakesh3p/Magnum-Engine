# MagnumMcp: the Model Context Protocol server of the engine (stdio JSON-RPC <-> the editor's control channel). It needs only
# nlohmann/json (already used) and Winsock; it does not link the engine.
add_executable(MagnumMcp main.cpp EditorLink.cpp Tools.cpp)
target_link_libraries(MagnumMcp PRIVATE nlohmann_json::nlohmann_json ws2_32)
target_compile_definitions(MagnumMcp PRIVATE MAGNUM_ENGINE_DIR="${CMAKE_SOURCE_DIR}")
magnum_target_defaults(MagnumMcp)
