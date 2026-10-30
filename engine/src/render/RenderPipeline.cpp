#include "render/RenderPipeline.h"

#include "core/Log.h"

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

struct TypeName {
    const char* name;
    PipelinePass::Type type;
    bool needsMaterial;
};

constexpr TypeName kTypes[] = {
    {"clear", PipelinePass::Type::Clear, false},     {"sky", PipelinePass::Type::Sky, true},
    {"meshes", PipelinePass::Type::Meshes, false},   {"sprites", PipelinePass::Type::Sprites, false}, {"decals", PipelinePass::Type::Decals, false}, {"markers", PipelinePass::Type::Markers, false},
    {"grid", PipelinePass::Type::Grid, false},       {"overlay", PipelinePass::Type::Overlay, true},
    {"post", PipelinePass::Type::Post, true},
    {"ui", PipelinePass::Type::Ui, false},
};

} // namespace

const char* PassTypeName(PipelinePass::Type type) {
    for (const TypeName& t : kTypes) {
        if (t.type == type) return t.name;
    }
    return "?";
}

PipelineData DefaultPipeline() {
    PipelineData d;
    d.name = "Built-in";
    for (const PipelinePass::Type t : {PipelinePass::Type::Clear, PipelinePass::Type::Meshes, PipelinePass::Type::Markers,
                                       PipelinePass::Type::Grid}) {
        PipelinePass p;
        p.type = t;
        d.passes.push_back(p);
    }
    return d;
}

bool ParsePipeline(const std::string& source, const std::string& displayName, PipelineData& out,
                   std::vector<ShaderDiagnostic>& diagnostics) {
    try {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        lua["pipeline"] = [](sol::table t) { return t; };

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
            diagnostics.push_back({displayName, 0, "the file must end with 'return pipeline { ... }'"});
            return false;
        }

        const sol::table root = result;
        PipelineData data;
        data.name = root.get_or<std::string>("name", "Pipeline");

        const sol::optional<sol::table> passes = root["passes"];
        if (!passes) {
            diagnostics.push_back({displayName, 0, "missing 'passes = { ... }'"});
            return false;
        }

        bool ok = true;
        for (size_t i = 1; i <= passes->size(); ++i) {
            const sol::optional<sol::table> entry = (*passes)[i];
            const std::string where = "pass " + std::to_string(i);
            if (!entry) {
                diagnostics.push_back({displayName, 0, where + " must be a table like { \"meshes\" }"});
                ok = false;
                continue;
            }
            const sol::optional<std::string> typeName = (*entry)[1];
            if (!typeName) {
                diagnostics.push_back({displayName, 0, where + ": the first entry must be the pass type, e.g. { \"sky\", material = \"...\" }"});
                ok = false;
                continue;
            }
            const TypeName* known = nullptr;
            for (const TypeName& t : kTypes) {
                if (*typeName == t.name) known = &t;
            }
            if (!known) {
                std::string names;
                for (const TypeName& t : kTypes) names += (names.empty() ? "" : ", ") + std::string(t.name);
                diagnostics.push_back({displayName, 0, where + ": unknown pass type '" + *typeName + "', available: " + names});
                ok = false;
                continue;
            }

            PipelinePass pass;
            pass.type = known->type;
            pass.material = entry->get_or<std::string>("material", "");
            pass.enabled = entry->get_or("enabled", true);
            pass.scale = entry->get_or("scale", 1.0f);
            if (!(pass.scale >= 0.25f && pass.scale <= 1.0f)) {
                diagnostics.push_back({displayName, 0, where + ": scale must be between 0.25 and 1"});
                ok = false;
            }
            if (known->needsMaterial && pass.material.empty()) {
                diagnostics.push_back({displayName, 0, where + " (" + *typeName + ") needs material = \"path/to/material.lua\""});
                ok = false;
            }
            if (const sol::optional<sol::table> color = (*entry)["color"]; color && color->size() == 3) {
                for (size_t c = 0; c < 3; ++c) pass.color[c] = (*color)[c + 1].get_or(0.0f);
            } else if (color) {
                diagnostics.push_back({displayName, 0, where + ": color must be {r, g, b}"});
                ok = false;
            }
            data.passes.push_back(pass);
        }
        if (!ok) return false;
        out = std::move(data);
        return true;
    } catch (const std::exception& e) {
        diagnostics.push_back({displayName, 0, std::string("internal error: ") + e.what()});
        return false;
    }
}

void RenderPipeline::Init(const std::filesystem::path& projectRoot, const std::string& relPath) {
    m_root = projectRoot;
    m_path = relPath;
    Reload();
}

std::filesystem::file_time_type RenderPipeline::Stamp() const {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(m_root / m_path, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

void RenderPipeline::Poll() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (now - m_lastPoll < milliseconds(250)) return;
    m_lastPoll = now;
    if (Stamp() != m_stamp) Reload();
}

bool RenderPipeline::Reload() {
    m_stamp = Stamp();
    std::vector<ShaderDiagnostic> diagnostics;

    std::ifstream in(m_root / m_path, std::ios::binary);
    if (!in) {
        // No script: the built-in passes stay in use.
        m_data = DefaultPipeline();
        m_diagnostics.clear();
        ++m_revision;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();
    StripUtf8Bom(source);

    PipelineData data;
    if (!ParsePipeline(source, m_path, data, diagnostics)) {
        m_diagnostics = diagnostics;
        for (const auto& d : diagnostics) Log::Error("{}", d.ToString());
        Log::Error("Pipeline '{}' is invalid, keeping the previous one", m_path);
        return false;
    }
    m_data = std::move(data);
    m_diagnostics.clear();
    ++m_revision;
    Log::Info("Pipeline '{}' loaded ({} pass(es))", m_data.name, m_data.passes.size());
    return true;
}

} // namespace magnum
