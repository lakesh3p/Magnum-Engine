#include "shader/LuaShaderCompiler.h"

#include "core/Log.h"

#include <sol/sol.hpp>

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace magnum {

namespace {

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    StripUtf8Bom(text);
    return text;
}

// Lua errors look like "Content/Shaders/x.lua:12: message".
ShaderDiagnostic ParseLuaError(const std::string& what, const std::string& fallbackFile) {
    const std::string firstLine = what.substr(0, what.find('\n'));
    static const std::regex pattern(R"(^(.+?):(\d+): (.*)$)");
    std::smatch m;
    ShaderDiagnostic d;
    if (std::regex_match(firstLine, m, pattern)) {
        d.file = m[1];
        d.line = std::stoi(m[2]);
        d.message = m[3];
    } else {
        d.file = fallbackFile;
        d.message = firstLine;
    }
    return d;
}

std::vector<int> ReadIntArray(const sol::table& t) {
    std::vector<int> out;
    for (size_t i = 1; i <= t.size(); ++i) {
        out.push_back(t[i].get<int>());
    }
    return out;
}

} // namespace

bool CompileLuaShader(const std::filesystem::path& luaFile, const std::string& displayName,
                      const std::filesystem::path& dslFile, const std::string& dslDisplayName,
                      CompiledShaderSource& out, std::vector<ShaderDiagnostic>& diagnostics) {
    const auto dslText = ReadFile(dslFile);
    if (!dslText) {
        diagnostics.push_back({dslDisplayName, 0, "cannot read the shader DSL file"});
        return false;
    }
    const auto scriptText = ReadFile(luaFile);
    if (!scriptText) {
        diagnostics.push_back({displayName, 0, "cannot read the shader file"});
        return false;
    }

    try {
        // No io/os/package: shader scripts cannot touch the filesystem.
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::debug);

        sol::load_result dslChunk = lua.load(*dslText, "@" + dslDisplayName);
        if (!dslChunk.valid()) {
            const sol::error e = dslChunk;
            diagnostics.push_back(ParseLuaError(e.what(), dslDisplayName));
            return false;
        }
        sol::protected_function dslMain = dslChunk;
        sol::protected_function_result dslResult = dslMain();
        if (!dslResult.valid()) {
            const sol::error e = dslResult;
            diagnostics.push_back(ParseLuaError(e.what(), dslDisplayName));
            return false;
        }

        sol::table dsl = dslResult;
        dsl["log"] = [](const std::string& text) { Log::Info("[shader] {}", text); };

        sol::protected_function compile = dsl["compile"];
        sol::protected_function_result result = compile(*scriptText, "@" + displayName);
        if (!result.valid()) {
            const sol::error e = result;
            diagnostics.push_back(ParseLuaError(e.what(), displayName));
            return false;
        }

        const sol::table t = result;
        out.name = t["name"].get<std::string>();
        out.luaFile = displayName;
        out.vertexGlsl = t["vertex"].get<std::string>();
        out.fragmentGlsl = t["fragment"].get<std::string>();
        out.vertexLineMap = ReadIntArray(t["vertex_map"]);
        out.fragmentLineMap = ReadIntArray(t["fragment_map"]);

        out.params.clear();
        const sol::table params = t["params"];
        for (size_t i = 1; i <= params.size(); ++i) {
            const sol::table p = params[i];
            ShaderParam sp;
            sp.name = p["name"].get<std::string>();
            sp.kind = p["kind"].get<std::string>();
            sp.uniform = p["uniform"].get<std::string>();
            sp.isTexture = sp.kind == "texture";
            sp.isColor = sp.kind == "color";
            if (sp.isTexture) {
                sp.components = 0;
                sp.defaultTexture = p["texture"].get<std::string>();
                sp.srgb = p["srgb"].get<bool>();
            } else {
                const std::string type = p["type"].get<std::string>();
                sp.components = type == "float" ? 1 : type.back() - '0';
                const sol::table def = p["default"];
                for (int c = 0; c < sp.components; ++c) {
                    sp.defaultValue[static_cast<size_t>(c)] = def[c + 1].get<float>();
                }
            }

            const sol::optional<float> lo = p["min"];
            const sol::optional<float> hi = p["max"];
            if (lo && hi) {
                sp.hasRange = true;
                sp.min = *lo;
                sp.max = *hi;
            }
            out.params.push_back(sp);
        }
    } catch (const std::exception& e) {
        // Binding-level failures (unexpected types coming back from the DSL).
        diagnostics.push_back({displayName, 0, std::string("internal DSL error: ") + e.what()});
        return false;
    }
    return true;
}

} // namespace magnum
