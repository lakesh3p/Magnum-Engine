#pragma once

#include "material/MaterialAsset.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace magnum {

// Loads material files on first use and hot-reloads all of them.
class MaterialLibrary {
public:
    void Init(const std::filesystem::path& projectRoot) { m_root = projectRoot; }
    void Poll();
    void Clear() { m_assets.clear(); }
    void ReloadAll();

    // nullptr if the file does not exist (reported once per path).
    MaterialAsset* Get(const std::string& relPath);

    // Sum of all asset revisions: changes whenever any material changes.
    uint64_t Revision() const;

private:
    std::filesystem::path m_root;
    std::map<std::string, std::unique_ptr<MaterialAsset>> m_assets;
    std::set<std::string> m_reportedMissing;
};

} // namespace magnum
