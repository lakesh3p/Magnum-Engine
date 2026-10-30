#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace magnum {

// One part of a model: triangles in the Mesh layout, 8 floats per vertex (position, normal, uv).
// A model with several materials or parts has several of these.
struct ImportedMesh {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

// A model file read from disk, in the file's own units and around the file's own origin. Scene nodes are baked into
// the vertices, so the whole file is a flat list of triangles. Materials and textures of the file are not used: a
// model is drawn with the material of the actor that shows it.
struct ImportedModel {
    std::vector<ImportedMesh> meshes;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    glm::vec3 Size() const { return boundsMax - boundsMin; }
};

// Every extension the importer reads, lower case with the dot (".obj", ".fbx", ".glb", ...).
const std::vector<std::string>& ModelFormats();
bool IsModelFile(const std::string& relPath);

// Reads a model file. Null on failure, with the reason in `error`.
std::shared_ptr<const ImportedModel> ImportModel(const std::filesystem::path& file, std::string& error);

// The model at a project-relative path (or "Engine/..."), read once and kept. A file that changed on disk is read
// again on the next call. Null when the file is missing or cannot be read; the error is logged once per change.
std::shared_ptr<const ImportedModel> LoadModel(const std::string& relPath);

} // namespace magnum
