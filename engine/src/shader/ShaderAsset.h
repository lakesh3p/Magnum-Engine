#pragma once

#include "shader/ShaderProgram.h"
#include "shader/ShaderTypes.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace magnum {

// A shader script on disk plus its compiled program. Poll() watches the script and the DSL
// file and recompiles on change. A failed recompile keeps the last working program.
class ShaderAsset {
public:
    // Paths are relative to projectRoot; they are also what diagnostics show.
    void Init(const std::filesystem::path& projectRoot, const std::string& luaFile, const std::string& dslFile);

    void Poll(); // cheap; call every frame
    bool Reload();

    const ShaderProgram* Program() const { return m_program.get(); }
    // The variant for instanced draws (see ShaderProgram::CreateInstanced); null when there is none.
    const ShaderProgram* InstancedProgram() const { return m_instanced.get(); }
    const std::string& Name() const { return m_name; }
    const std::string& File() const { return m_luaFile; }
    const std::vector<ShaderParam>& Params() const { return m_params; }
    const std::vector<ShaderDiagnostic>& Diagnostics() const { return m_diagnostics; }
    // Increases on every successful (re)compile; materials use it to re-resolve their parameters.
    uint64_t Revision() const { return m_revision; }

private:
    using FileTime = std::filesystem::file_time_type;
    FileTime Stamp(const std::filesystem::path& p) const;

    std::filesystem::path m_root;
    std::string m_luaFile;
    std::string m_dslFile;
    FileTime m_luaStamp{};
    FileTime m_dslStamp{};
    std::chrono::steady_clock::time_point m_lastPoll{};

    uint64_t m_revision = 0;
    std::unique_ptr<ShaderProgram> m_program;
    std::unique_ptr<ShaderProgram> m_instanced;
    std::string m_name;
    std::vector<ShaderParam> m_params;
    std::vector<ShaderDiagnostic> m_diagnostics;
};

} // namespace magnum
