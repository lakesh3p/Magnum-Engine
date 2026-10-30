#include "model/ModelImport.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>

namespace magnum {

namespace fs = std::filesystem;

namespace {

// Assimp opens files with the ANSI code page, so folders and names with non-Latin letters would not be found. These
// classes read through std::filesystem, which takes the UTF-8 names as they are.
fs::path ToPath(const char* utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8), std::strlen(utf8)));
}

std::string ToUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

class MemoryStream : public Assimp::IOStream {
public:
    explicit MemoryStream(std::vector<char> data) : m_data(std::move(data)) {}

    size_t Read(void* buffer, size_t size, size_t count) override {
        if (size == 0) return 0;
        const size_t bytes = std::min(size * count, m_data.size() - std::min(m_pos, m_data.size()));
        std::memcpy(buffer, m_data.data() + m_pos, bytes);
        m_pos += bytes;
        return bytes / size;
    }
    size_t Write(const void*, size_t, size_t) override { return 0; } // read only
    aiReturn Seek(size_t offset, aiOrigin origin) override {
        const size_t base = origin == aiOrigin_SET ? 0 : origin == aiOrigin_CUR ? m_pos : m_data.size();
        const size_t target = base + offset; // a negative offset arrives as a wrapped size_t: the sum still lands right
        if (target > m_data.size()) return aiReturn_FAILURE;
        m_pos = target;
        return aiReturn_SUCCESS;
    }
    size_t Tell() const override { return m_pos; }
    size_t FileSize() const override { return m_data.size(); }
    void Flush() override {}

private:
    std::vector<char> m_data;
    size_t m_pos = 0;
};

class FileSystem : public Assimp::IOSystem {
public:
    bool Exists(const char* path) const override {
        std::error_code ec;
        return fs::is_regular_file(ToPath(path), ec);
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* path, const char* mode) override {
        if (mode && mode[0] != 'r') return nullptr; // read only
        std::ifstream in(ToPath(path), std::ios::binary);
        if (!in) return nullptr;
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return new MemoryStream(std::move(data));
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }
};

// Everything the renderer needs, and nothing that depends on the file format: triangles, normals, first UV set.
// GlobalScale converts the file's unit (FBX files are often in centimetres) to metres, one unit in the scene.
constexpr unsigned kImportFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
                                  aiProcess_FlipUVs | aiProcess_PreTransformVertices | aiProcess_ImproveCacheLocality |
                                  aiProcess_GlobalScale;

void Collect(const aiScene& scene, ImportedModel& model) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (unsigned m = 0; m < scene.mNumMeshes; ++m) {
        const aiMesh* mesh = scene.mMeshes[m];
        if (!mesh->HasPositions() || !mesh->HasFaces()) continue;

        ImportedMesh part;
        part.vertices.reserve(static_cast<size_t>(mesh->mNumVertices) * 8);
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            const aiVector3D& p = mesh->mVertices[v];
            const aiVector3D n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0.0f, 1.0f, 0.0f);
            const aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(0.0f);
            part.vertices.insert(part.vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
            lo = glm::min(lo, glm::vec3(p.x, p.y, p.z));
            hi = glm::max(hi, glm::vec3(p.x, p.y, p.z));
        }
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue; // triangulated on import; anything else is skipped
            part.indices.insert(part.indices.end(), {face.mIndices[0], face.mIndices[1], face.mIndices[2]});
        }
        if (!part.indices.empty()) model.meshes.push_back(std::move(part));
    }
    if (!model.meshes.empty()) {
        model.boundsMin = lo;
        model.boundsMax = hi;
    }
}

} // namespace

const std::vector<std::string>& ModelFormats() {
    static const std::vector<std::string> formats = [] {
        // Assimp lists its readers as "*.3ds;*.obj;..." (lower case). Turn that into ".3ds", ".obj", ...
        aiString list;
        Assimp::Importer importer;
        importer.GetExtensionList(list);
        std::vector<std::string> out;
        std::istringstream in(list.C_Str());
        for (std::string item; std::getline(in, item, ';');) {
            if (item.size() > 1 && item[0] == '*') out.push_back(item.substr(1));
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return formats;
}

bool IsModelFile(const std::string& relPath) {
    std::string ext = fs::path(relPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto& formats = ModelFormats();
    return !ext.empty() && std::find(formats.begin(), formats.end(), ext) != formats.end();
}

std::shared_ptr<const ImportedModel> ImportModel(const fs::path& file, std::string& error) {
    Assimp::Importer importer;
    importer.SetIOHandler(new FileSystem);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);

    const aiScene* scene = importer.ReadFile(ToUtf8(file), kImportFlags);
    if (!scene) {
        error = importer.GetErrorString();
        return nullptr;
    }
    auto model = std::make_shared<ImportedModel>();
    Collect(*scene, *model);
    if (model->meshes.empty()) {
        error = "the file has no triangles";
        return nullptr;
    }
    return model;
}

namespace {

struct Cached {
    fs::file_time_type stamp{};
    std::shared_ptr<const ImportedModel> model; // null when the file could not be read
    std::string error;
    std::chrono::steady_clock::time_point retryAt{}; // a failed file is read again after this time
};

} // namespace

std::shared_ptr<const ImportedModel> LoadModel(const std::string& relPath) {
    static std::map<std::string, Cached> cache;
    if (relPath.empty()) return nullptr;

    const fs::path path = ResolveAsset(ProjectRoot(), relPath);
    std::error_code ec;
    const fs::file_time_type stamp = fs::last_write_time(path, ec);
    if (ec) { // missing: forget what was loaded for it
        cache.erase(relPath);
        return nullptr;
    }
    const auto now = std::chrono::steady_clock::now();
    auto it = cache.find(relPath);
    if (it != cache.end() && it->second.stamp == stamp) {
        // A failed file is tried again now and then: a glTF's .bin or textures may have arrived since.
        if (it->second.model || now < it->second.retryAt) return it->second.model;
    }

    std::string error;
    std::shared_ptr<const ImportedModel> model = ImportModel(path, error);
    Cached& entry = cache[relPath];
    if (!model && error != entry.error) Log::Error("Cannot read model {}: {}", relPath, error); // once per reason
    entry = Cached{stamp, model, error, now + std::chrono::seconds(1)};
    return model;
}

} // namespace magnum
