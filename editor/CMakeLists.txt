add_executable(MagnumEditor
    src/main.cpp
    src/ControlServer.cpp
    src/EditorApp.cpp
    src/EditorControl.cpp
    src/EditorCamera.cpp
    src/Icons.cpp
    src/Picking.cpp
    src/PluginHost.cpp
    src/Theme.cpp
    src/ThumbnailCache.cpp
    src/commands/CommandStack.cpp
    src/commands/EditorCommands.cpp
    src/commands/GraphCommands.cpp
    src/commands/MaterialCommands.cpp
    src/commands/SpawnKinds.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ContentBrowserPanel.cpp
    src/panels/DetailsPanel.cpp
    src/panels/OutlinerPanel.cpp
    src/panels/PlaceActorsPanel.cpp
    src/panels/MaterialGraphPanel.cpp
    src/panels/MaterialInspector.cpp
    src/panels/MaterialPanel.cpp
    src/panels/UiPanel.cpp
    src/panels/ViewportPanel.cpp)

target_include_directories(MagnumEditor PRIVATE src)
target_link_libraries(MagnumEditor PRIVATE magnum_core imgui imguizmo imnodes sol2 lua54 nlohmann_json::nlohmann_json ws2_32)
# The engine folder (Engine/ assets, fonts, scripts, Projects/). A project is chosen at run time (--project, File menu).
target_compile_definitions(MagnumEditor PRIVATE MAGNUM_ENGINE_DIR="${CMAKE_SOURCE_DIR}")
magnum_target_defaults(MagnumEditor)
