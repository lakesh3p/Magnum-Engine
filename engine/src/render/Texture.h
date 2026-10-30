#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace magnum {

struct Texture {
    uint32_t id = 0; // GL texture name, 0 = failed to load
    int width = 0;
    int height = 0;
};

// Loads PNG/JPG/TGA/BMP files from the project and hot-reloads them when the file changes.
// The same file can be loaded twice, as sRGB color (materials) or as raw values (UI previews, data maps).
class TextureLibrary {
public:
    ~TextureLibrary() { Clear(); }

    void Init(const std::filesystem::path& projectRoot);
    void Poll();
    void Clear(); // releases GL objects; call while the GL context is still alive

    // Never null; check .id. Failures are logged once and retried when the file changes.
    const Texture& Get(const std::string& relPath, bool srgb);
    uint32_t White() const { return m_white; }

    // Increases whenever a texture is (re)loaded.
    uint64_t Revision() const { return m_revision; }

    static bool IsImageFile(const std::string& path);

private:
    struct Entry {
        Texture texture;
        std::filesystem::file_time_type stamp{};
        bool srgb = true;
    };
    bool Load(const std::string& relPath, Entry& entry);

    std::filesystem::path m_root;
    std::map<std::string, Entry> m_entries; // key: path + (srgb ? "|srgb" : "|raw")
    uint32_t m_white = 0;
    uint64_t m_revision = 0;
    std::chrono::steady_clock::time_point m_lastPoll{};
};

} // namespace magnum
