#pragma once

#include "shader/ShaderAsset.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace magnum {

// Loads shader scripts on first use and hot-reloads all of them.
class ShaderLibrary {
public:
    static constexpr const char* kFallback = "Content/Shaders/default_lit.lua";

    void Init(const std::filesystem::path& projectRoot, const std::string& dslFile);
    void Poll();
    void Clear() { m_assets.clear(); }
    void ReloadAll();

    // Sum of all asset revisions: changes whenever any shader is recompiled.
    uint64_t Revision() const;

    // nullptr if the file does not exist (reported once per path). A script that exists
    // but fails to compile still returns its asset; its Program() may be null.
    ShaderAsset* Get(const std::string& luaFile);

private:
    std::filesystem::path m_root;
    std::string m_dslFile;
    std::map<std::string, std::unique_ptr<ShaderAsset>> m_assets;
    std::set<std::string> m_reportedMissing;
};

} // namespace magnum
