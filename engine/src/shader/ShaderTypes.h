#pragma once

#include <array>
#include <string>
#include <vector>

namespace magnum {

// Notepad and many editors save UTF-8 files with a byte order mark; Lua does not accept it.
inline void StripUtf8Bom(std::string& text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
}

// An error or warning pointing at a line of the user's .lua file (line 0 = unknown).
struct ShaderDiagnostic {
    std::string file;
    int line = 0;
    std::string message;

    std::string ToString() const {
        return line > 0 ? file + ":" + std::to_string(line) + ": " + message : file + ": " + message;
    }
};

// A parameter declared with S:param(...) in a shader script.
struct ShaderParam {
    std::string name;
    std::string kind;    // "float", "vec2", "vec3", "vec4", "color" or "texture"
    std::string uniform; // GLSL uniform name
    int components = 1;  // 0 for textures
    bool isColor = false;
    bool isTexture = false;
    bool srgb = true;               // textures: decode as sRGB when sampling
    std::string defaultTexture;     // textures: path used when a material sets none ("" = white)
    bool hasRange = false;
    float min = 0.0f;
    float max = 1.0f;
    std::array<float, 4> defaultValue{};
};

// Result of running a shader script through the Lua DSL.
struct CompiledShaderSource {
    std::string name;
    std::string luaFile; // path shown in diagnostics
    std::string vertexGlsl;
    std::string fragmentGlsl;
    std::vector<int> vertexLineMap;   // GLSL line (1-based) -> .lua line, 0 if none
    std::vector<int> fragmentLineMap;
    std::vector<ShaderParam> params;
};

} // namespace magnum
