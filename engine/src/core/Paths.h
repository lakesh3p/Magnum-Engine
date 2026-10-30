#pragma once

#include <filesystem>
#include <string>

namespace magnum {

// Two folders hold files: the engine folder (Engine/Lua, Engine/Shaders, Engine/Fonts) and the open project
// (Content, Game, Plugins, Saved). Asset paths are written relative to the project; those that start
// with "Engine/" live in the engine folder.
void SetEngineRoot(const std::filesystem::path& root);
const std::filesystem::path& EngineRoot();

// The open project's folder, for code that only has a project-relative path (image sizes of sprites).
void SetProjectRoot(const std::filesystem::path& root);
const std::filesystem::path& ProjectRoot();

// The file a project-relative asset path refers to.
std::filesystem::path ResolveAsset(const std::filesystem::path& projectRoot, const std::string& relPath);

} // namespace magnum
