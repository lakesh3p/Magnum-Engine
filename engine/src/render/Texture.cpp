#include "render/Texture.h"

#include "core/Log.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include <stb_image.h>

namespace magnum {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

bool TextureLibrary::IsImageFile(const std::string& path) {
    const std::string ext = Lower(std::filesystem::path(path).extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

void TextureLibrary::Init(const std::filesystem::path& projectRoot) {
    m_root = projectRoot;
    if (!m_white) {
        const uint8_t pixel[4] = {255, 255, 255, 255};
        glCreateTextures(GL_TEXTURE_2D, 1, &m_white);
        glTextureStorage2D(m_white, 1, GL_RGBA8, 1, 1);
        glTextureSubImage2D(m_white, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    }
}

void TextureLibrary::Clear() {
    for (auto& [key, entry] : m_entries) {
        if (entry.texture.id) glDeleteTextures(1, &entry.texture.id);
    }
    m_entries.clear();
    if (m_white) glDeleteTextures(1, &m_white);
    m_white = 0;
}

bool TextureLibrary::Load(const std::string& relPath, Entry& entry) {
    const std::filesystem::path path = m_root / relPath;
    std::error_code ec;
    entry.stamp = std::filesystem::last_write_time(path, ec);

    // Read the file ourselves so non-ASCII paths work (stb would use the ANSI code page).
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Log::Error("Texture not found: {}", relPath);
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    stbi_set_flip_vertically_on_load(1); // OpenGL's v = 0 is the bottom row
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        Log::Error("Cannot read texture {}: {}", relPath, stbi_failure_reason());
        return false;
    }

    GLuint id = 0;
    const int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<double>(std::max(w, h)))));
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    glTextureStorage2D(id, levels, entry.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(id, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateTextureMipmap(id);
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    }
    stbi_image_free(pixels);

    if (entry.texture.id) glDeleteTextures(1, &entry.texture.id);
    entry.texture = {id, w, h};
    Log::Info("Texture loaded: {} ({}x{}, {})", relPath, w, h, entry.srgb ? "sRGB" : "raw");
    return true;
}

const Texture& TextureLibrary::Get(const std::string& relPath, bool srgb) {
    const std::string key = relPath + (srgb ? "|srgb" : "|raw");
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        Entry entry;
        entry.srgb = srgb;
        Load(relPath, entry);
        ++m_revision;
        it = m_entries.emplace(key, std::move(entry)).first;
    }
    return it->second.texture;
}

void TextureLibrary::Poll() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (now - m_lastPoll < milliseconds(300)) return;
    m_lastPoll = now;

    for (auto& [key, entry] : m_entries) {
        const std::string relPath = key.substr(0, key.rfind('|'));
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(m_root / relPath, ec);
        if (!ec && stamp != entry.stamp) {
            Load(relPath, entry); // on failure the previous image stays
            ++m_revision;
        }
    }
}

} // namespace magnum
