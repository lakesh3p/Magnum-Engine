#pragma once

#include "scene/Components.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace magnum {

// One copy of a mesh in an instanced draw: the object -> world matrix and its normal matrix. The shader DSL reads
// them as instance attributes (locations 9..12 and 13..15, see generate() in shader_dsl.lua). GL allows 16 attributes.
struct MeshInstance {
    float model[16];  // column-major mat4
    float normal[12]; // column-major mat3, each column padded to four floats
};

// Indexed GPU mesh. Every vertex is 8 floats: position (location 0), normal (1), uv (2),
// which is the layout the shader DSL's standard attributes expect.
class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Create(const std::vector<float>& vertices, const std::vector<uint32_t>& indices, uint32_t glMode);
    void BuildPrimitive(Primitive primitive);
    void BuildLineBox(); // unit cube edges, drawn with GL_LINES
    void Destroy();
    void Draw() const;
    // Draws the mesh once per MeshInstance in `instanceBuffer` (an array of MeshInstance). One draw call for all.
    void DrawInstanced(uint32_t instanceBuffer, int count) const;

private:
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ebo = 0;
    uint32_t m_mode = 0;
    int m_indexCount = 0;
};

} // namespace magnum
