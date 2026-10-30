#include "render/ShaderLibrary.h"

#include "core/Paths.h"

#include "core/Log.h"

namespace magnum {

void ShaderLibrary::Init(const std::filesystem::path& projectRoot, const std::string& dslFile) {
    m_root = projectRoot;
    m_dslFile = dslFile;
}

void ShaderLibrary::Poll() {
    for (auto& [path, asset] : m_assets) {
        asset->Poll();
    }
}

void ShaderLibrary::ReloadAll() {
    for (auto& [path, asset] : m_assets) asset->Reload();
}

uint64_t ShaderLibrary::Revision() const {
    uint64_t sum = m_assets.size();
    for (const auto& [path, asset] : m_assets) sum += asset->Revision();
    return sum;
}

ShaderAsset* ShaderLibrary::Get(const std::string& luaFile) {
    if (const auto it = m_assets.find(luaFile); it != m_assets.end()) {
        return it->second.get();
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(ResolveAsset(m_root, luaFile), ec)) {
        if (m_reportedMissing.insert(luaFile).second) {
            Log::Warn("Shader file not found: {}", luaFile);
        }
        return nullptr;
    }
    auto asset = std::make_unique<ShaderAsset>();
    asset->Init(m_root, luaFile, m_dslFile);
    ShaderAsset* raw = asset.get();
    m_assets[luaFile] = std::move(asset);
    return raw;
}

} // namespace magnum
