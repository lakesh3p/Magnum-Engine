#include "material/MaterialData.h"

#include <sol/sol.hpp>

#include <fstream>
#include <regex>
#include <sstream>

namespace magnum {

namespace {

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

} // namespace

bool LoadMaterialFile(const std::filesystem::path& file, const std::string& displayName, MaterialData& out,
                      std::vector<ShaderDiagnostic>& diagnostics) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        diagnostics.push_back({displayName, 0, "cannot read the material file"});
        return false;
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    std::string source = stream.str();
    StripUtf8Bom(source);

    try {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        // `material { ... }` just returns its table; the structure is checked below.
        lua["material"] = [](sol::table t) { return t; };

        sol::load_result chunk = lua.load(source, "@" + displayName);
        if (!chunk.valid()) {
            const sol::error e = chunk;
            diagnostics.push_back(ParseLuaError(e.what(), displayName));
            return false;
        }
        sol::protected_function run = chunk;
        sol::protected_function_result result = run();
        if (!result.valid()) {
            const sol::error e = result;
            diagnostics.push_back(ParseLuaError(e.what(), displayName));
            return false;
        }
        if (result.get_type() != sol::type::table) {
            diagnostics.push_back({displayName, 0, "the file must end with 'return material { ... }'"});
            return false;
        }

        const sol::table root = result;
        MaterialData data;
        const sol::optional<std::string> shader = root["shader"];
        if (!shader || shader->empty()) {
            diagnostics.push_back({displayName, 0, "missing 'shader = \"path/to/shader.lua\"'"});
            return false;
        }
        data.shader = *shader;

        bool ok = true;
        const sol::optional<sol::table> params = root["params"];
        if (params) {
            for (const auto& [key, value] : *params) {
                if (key.get_type() != sol::type::string) {
                    diagnostics.push_back({displayName, 0, "parameter names must be identifiers"});
                    ok = false;
                    continue;
                }
                const std::string name = key.as<std::string>();
                if (value.get_type() == sol::type::number) {
                    data.SetNumber(name, 1, {value.as<float>(), 0, 0, 0});
                } else if (value.get_type() == sol::type::string) {
                    data.SetTexture(name, value.as<std::string>());
                } else if (value.get_type() == sol::type::table) {
                    const sol::table t = value.as<sol::table>();
                    const size_t n = t.size();
                    std::array<float, 4> numbers{};
                    bool valid = n >= 2 && n <= 4;
                    for (size_t i = 1; valid && i <= n; ++i) {
                        const sol::optional<float> f = t[i];
                        if (!f) valid = false;
                        else numbers[i - 1] = *f;
                    }
                    if (valid) {
                        data.SetNumber(name, static_cast<int>(n), numbers);
                    } else {
                        diagnostics.push_back({displayName, 0, "parameter '" + name + "' must be a list of 2-4 numbers"});
                        ok = false;
                    }
                } else {
                    diagnostics.push_back({displayName, 0, "parameter '" + name + "' must be a number, a list of numbers or a texture path"});
                    ok = false;
                }
            }
        }
        if (!ok) return false;
        out = std::move(data);
        return true;
    } catch (const std::exception& e) {
        diagnostics.push_back({displayName, 0, std::string("internal error: ") + e.what()});
        return false;
    }
}

} // namespace magnum
