#pragma once

#include "shader/ShaderTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace magnum {

// Runs a shader .lua script through the DSL (shader_dsl.lua) and returns the generated GLSL.
// Uses a fresh Lua state every time, so no state leaks between reloads.
// displayName / dslDisplayName are the paths shown in error messages.
bool CompileLuaShader(const std::filesystem::path& luaFile, const std::string& displayName,
                      const std::filesystem::path& dslFile, const std::string& dslDisplayName,
                      CompiledShaderSource& out, std::vector<ShaderDiagnostic>& diagnostics);

} // namespace magnum
