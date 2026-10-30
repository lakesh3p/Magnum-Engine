#pragma once

#include "material/MaterialData.h"
#include "shader/ShaderAsset.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace magnum {

// A material file on disk: a shader plus parameter values. Hot-reloads when the file changes;
// edits made in the editor are written back with Save().
class MaterialAsset {
public:
    // A shader parameter with the value this material gives it (override or the shader's default).
    struct Resolved {
        const ShaderParam* param = nullptr;
        std::array<float, 4> value{};
        std::string texture;
    };

    void Init(const std::filesystem::path& projectRoot, const std::string& relPath);
    void Poll();
    bool Reload();
    bool Save(); // rewrites the file from Data(); does not trigger a reload of itself

    const std::string& Path() const { return m_path; }
    const MaterialData& Data() const { return m_data; }
    void SetData(const MaterialData& data); // replaces the data in memory (callers Save() when done editing)
    const std::vector<ShaderDiagnostic>& Diagnostics() const { return m_diagnostics; }
    // Increases whenever the data changes in memory or on disk.
    uint64_t Revision() const { return m_revision; }

    // The parameter list of `shader` filled with this material's values. Mismatches (unknown names,
    // wrong type) are reported once and ignored. Cached until the material or the shader changes.
    const std::vector<Resolved>& Resolve(const ShaderAsset& shader);

private:
    using FileTime = std::filesystem::file_time_type;
    FileTime Stamp() const;

    std::filesystem::path m_root;
    std::string m_path;
    FileTime m_stamp{};
    std::chrono::steady_clock::time_point m_lastPoll{};

    MaterialData m_data;
    std::vector<ShaderDiagnostic> m_diagnostics;
    uint64_t m_revision = 0;

    std::set<std::string> m_warned; // parameter problems already reported (per shader revision)
    std::vector<Resolved> m_resolved;
    const ShaderAsset* m_resolvedFor = nullptr;
    uint64_t m_resolvedRevision = ~0ull;
    uint64_t m_resolvedShaderRevision = ~0ull;
};

} // namespace magnum
