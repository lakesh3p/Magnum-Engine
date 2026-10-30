#include "material/MaterialLibrary.h"

#include "core/Log.h"
#include "core/Paths.h"

namespace magnum {

void MaterialLibrary::Poll() {
    for (auto& [path, asset] : m_assets) asset->Poll();
}

void MaterialLibrary::ReloadAll() {
    for (auto& [path, asset] : m_assets) asset->Reload();
}

MaterialAsset* MaterialLibrary::Get(const std::string& relPath) {
    if (const auto it = m_assets.find(relPath); it != m_assets.end()) return it->second.get();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(ResolveAsset(m_root, relPath), ec)) {
        if (m_reportedMissing.insert(relPath).second) Log::Warn("Material file not found: {}", relPath);
        return nullptr;
    }
    auto asset = std::make_unique<MaterialAsset>();
    asset->Init(m_root, relPath);
    MaterialAsset* raw = asset.get();
    m_assets[relPath] = std::move(asset);
    return raw;
}

uint64_t MaterialLibrary::Revision() const {
    uint64_t sum = m_assets.size();
    for (const auto& [path, asset] : m_assets) sum += asset->Revision();
    return sum;
}

} // namespace magnum
