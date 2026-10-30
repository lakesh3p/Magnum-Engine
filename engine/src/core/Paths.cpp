#include "core/Paths.h"

namespace magnum {

namespace {
std::filesystem::path g_engineRoot;
std::filesystem::path g_projectRoot;
}

void SetProjectRoot(const std::filesystem::path& root) {
    g_projectRoot = root;
}

const std::filesystem::path& ProjectRoot() {
    return g_projectRoot;
}

void SetEngineRoot(const std::filesystem::path& root) {
    g_engineRoot = root;
}

const std::filesystem::path& EngineRoot() {
    return g_engineRoot;
}

std::filesystem::path ResolveAsset(const std::filesystem::path& projectRoot, const std::string& relPath) {
    if (relPath.rfind("Engine/", 0) == 0 && !g_engineRoot.empty()) return g_engineRoot / relPath;
    return projectRoot / relPath;
}

} // namespace magnum
