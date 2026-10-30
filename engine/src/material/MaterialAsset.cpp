#include "material/MaterialAsset.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <fstream>

namespace magnum {

void MaterialAsset::Init(const std::filesystem::path& projectRoot, const std::string& relPath) {
    m_root = projectRoot;
    m_path = relPath;
    Reload();
}

MaterialAsset::FileTime MaterialAsset::Stamp() const {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(ResolveAsset(m_root, m_path), ec);
    return ec ? FileTime{} : t;
}

void MaterialAsset::Poll() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (now - m_lastPoll < milliseconds(250)) return;
    m_lastPoll = now;
    if (Stamp() != m_stamp) Reload();
}

bool MaterialAsset::Reload() {
    m_stamp = Stamp(); // recorded first so a broken file is not re-read until it changes again

    MaterialData loaded;
    std::vector<ShaderDiagnostic> diagnostics;
    if (!LoadMaterialFile(ResolveAsset(m_root, m_path), m_path, loaded, diagnostics)) {
        m_diagnostics = diagnostics;
        for (const auto& d : diagnostics) Log::Error("{}", d.ToString());
        Log::Error("Material '{}' failed to load{}", m_path, m_revision ? ", keeping the previous version" : "");
        ++m_revision; // the panel shows the new errors
        return false;
    }
    m_data = std::move(loaded);
    m_diagnostics.clear();
    ++m_revision;
    Log::Info("Material '{}' loaded (shader {}, {} override(s))", m_path, m_data.shader, m_data.params.size());
    return true;
}

void MaterialAsset::SetData(const MaterialData& data) {
    m_data = data;
    ++m_revision;
}

bool MaterialAsset::Save() {
    const std::filesystem::path file = ResolveAsset(m_root, m_path);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) {
            Log::Error("Cannot write {}", temp.string());
            return false;
        }
        out << FormatMaterial(m_data);
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        Log::Error("Cannot replace {}: {}", m_path, ec.message());
        return false;
    }
    m_stamp = Stamp(); // our own write must not trigger a reload
    m_diagnostics.clear();
    return true;
}

const std::vector<MaterialAsset::Resolved>& MaterialAsset::Resolve(const ShaderAsset& shader) {
    if (m_resolvedFor == &shader && m_resolvedRevision == m_revision && m_resolvedShaderRevision == shader.Revision()) {
        return m_resolved;
    }
    m_resolvedFor = &shader;
    m_resolvedRevision = m_revision;
    m_resolvedShaderRevision = shader.Revision();

    m_resolved.clear();
    for (const ShaderParam& p : shader.Params()) {
        Resolved r;
        r.param = &p;
        r.value = p.defaultValue;
        r.texture = p.defaultTexture;
        if (const MaterialValue* v = m_data.Find(p.name)) {
            if (p.isTexture && v->IsTexture()) {
                r.texture = v->texture;
            } else if (!p.isTexture && !v->IsTexture() && v->components == p.components) {
                r.value = v->number;
            } else if (m_warned.insert(p.name + "|type|" + std::to_string(shader.Revision())).second) {
                Log::Warn("{}: parameter '{}' does not match the shader ({} expected), using the shader default", m_path,
                          p.name, p.kind);
            }
        }
        m_resolved.push_back(std::move(r));
    }
    for (const MaterialValue& v : m_data.params) {
        bool known = false;
        for (const ShaderParam& p : shader.Params()) known = known || p.name == v.name;
        if (!known && m_warned.insert(v.name + "|unknown|" + std::to_string(shader.Revision())).second) Log::Warn("{}: the shader has no parameter '{}', ignored", m_path, v.name);
    }
    return m_resolved;
}

} // namespace magnum
