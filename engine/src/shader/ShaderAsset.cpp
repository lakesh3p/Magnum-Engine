#include "shader/ShaderAsset.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "shader/LuaShaderCompiler.h"

#include <fstream>

namespace magnum {

void ShaderAsset::Init(const std::filesystem::path& projectRoot, const std::string& luaFile,
                       const std::string& dslFile) {
    m_root = projectRoot;
    m_luaFile = luaFile;
    m_dslFile = dslFile;
    Reload();
}

ShaderAsset::FileTime ShaderAsset::Stamp(const std::filesystem::path& p) const {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(ResolveAsset(m_root, p.generic_string()), ec);
    return ec ? FileTime{} : t;
}

void ShaderAsset::Poll() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (now - m_lastPoll < milliseconds(250)) {
        return;
    }
    m_lastPoll = now;

    if (Stamp(m_luaFile) != m_luaStamp || Stamp(m_dslFile) != m_dslStamp) {
        Reload();
    }
}

bool ShaderAsset::Reload() {
    using namespace std::chrono;
    // Record the stamps first so a broken script is not recompiled until it changes again.
    m_luaStamp = Stamp(m_luaFile);
    m_dslStamp = Stamp(m_dslFile);

    const auto start = steady_clock::now();
    CompiledShaderSource source;
    std::vector<ShaderDiagnostic> diagnostics;

    std::unique_ptr<ShaderProgram> program;
    std::unique_ptr<ShaderProgram> instanced;
    if (CompileLuaShader(ResolveAsset(m_root, m_luaFile), m_luaFile, ResolveAsset(m_root, m_dslFile), m_dslFile, source, diagnostics)) {
        program = ShaderProgram::Create(source, diagnostics);
        if (program) {
            // Without the instanced variant the shader still works, its actors are just drawn one by one.
            std::vector<ShaderDiagnostic> instancedDiagnostics;
            instanced = ShaderProgram::CreateInstanced(source, instancedDiagnostics);
            if (!instanced) Log::Warn("Shader '{}' has no instanced variant, its actors are drawn one by one", m_luaFile);
        }
    }

    if (!program) {
        m_diagnostics = diagnostics;
        for (const auto& d : diagnostics) {
            Log::Error("{}", d.ToString());
        }
        Log::Error("Shader '{}' failed to compile{}", m_luaFile,
                   m_program ? ", keeping the previous version" : "");
        return false;
    }

    // Keep the generated GLSL on disk so it can be read next to the .lua it came from.
    {
        const std::filesystem::path stem = std::filesystem::path(m_luaFile).stem();
        const std::filesystem::path dir = m_root / "Saved" / "Generated";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream(dir / (stem.string() + ".vert.glsl"), std::ios::binary) << source.vertexGlsl;
        std::ofstream(dir / (stem.string() + ".frag.glsl"), std::ios::binary) << source.fragmentGlsl;
    }

    m_program = std::move(program);
    m_instanced = std::move(instanced);
    m_params = std::move(source.params);
    m_name = source.name;
    m_diagnostics.clear();
    ++m_revision;

    const double ms = duration<double, std::milli>(steady_clock::now() - start).count();
    Log::Info("Shader '{}' compiled ({}, {:.1f} ms, {} param(s))", m_name, m_luaFile, ms, m_params.size());
    return true;
}

} // namespace magnum
