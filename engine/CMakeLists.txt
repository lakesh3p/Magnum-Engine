add_library(magnum_core STATIC
    src/core/Log.cpp
    src/core/Paths.cpp
    src/game/GameBuilder.cpp
    src/game/Gameplay.cpp
    src/game/GameRuntime.cpp
    src/game/Physics.cpp
    src/material/MaterialAsset.cpp
    src/material/MaterialData.cpp
    src/material/MaterialLibrary.cpp
    src/material/MaterialLoader.cpp
    src/model/ModelImport.cpp
    src/platform/FileOps.cpp
    src/platform/Window.cpp
    src/project/Project.cpp
    src/render/Framebuffer.cpp
    src/render/Mesh.cpp
    src/render/PrimitiveShapes.cpp
    src/render/RenderPipeline.cpp
    src/render/SceneRenderer.cpp
    src/render/Screenshot.cpp
    src/render/ShaderLibrary.cpp
    src/render/Texture.cpp
    src/render/UiRenderer.cpp
    src/scene/ActorSnapshot.cpp
    src/scene/Reflection.cpp
    src/scene/Scene.cpp
    src/scene/SpriteSize.cpp
    src/scene/SceneSerializer.cpp
    src/shader/GraphLua.cpp
    src/shader/LuaShaderCompiler.cpp
    src/shader/MaterialGraph.cpp
    src/shader/ShaderAsset.cpp
    src/shader/ShaderProgram.cpp
    src/ui/UiDraw.cpp
    src/ui/UiFonts.cpp
    src/ui/UiInput.cpp
    src/ui/UiLayout.cpp
    src/ui/UiLua.cpp
    src/ui/UiPreview.cpp
    src/ui/UiSystem.cpp)

# `include` holds magnum_game_api.h, the C header shared with the game module.
target_include_directories(magnum_core PUBLIC src include)
target_compile_definitions(magnum_core PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(magnum_core PUBLIC glad glfw glm::glm EnTT::EnTT PRIVATE stb sol2 lua54 nlohmann_json::nlohmann_json assimp shell32 ole32)
magnum_target_defaults(magnum_core)
