#include "render/Mesh.h"

#include "render/PrimitiveShapes.h"

#include <glad/gl.h>

#include <cmath>

namespace magnum {

namespace {

constexpr float kPi = 3.14159265358979f;

void PushVertex(std::vector<float>& v, glm::vec3 p, glm::vec3 n, float u, float t) {
    v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, t});
}

void BuildCube(std::vector<float>& v, std::vector<uint32_t>& idx) {
    struct Face { glm::vec3 n, u; };
    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}}, {{-1, 0, 0}, {0, 0, 1}}, {{0, 1, 0}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}}, {{0, 0, 1}, {1, 0, 0}},  {{0, 0, -1}, {-1, 0, 0}},
    };
    for (const Face& f : faces) {
        const glm::vec3 w = glm::cross(f.n, f.u); // u x w = n, so the winding faces outward
        const uint32_t base = static_cast<uint32_t>(v.size() / 8);
        PushVertex(v, f.n * 0.5f - f.u * 0.5f - w * 0.5f, f.n, 0, 0);
        PushVertex(v, f.n * 0.5f + f.u * 0.5f - w * 0.5f, f.n, 1, 0);
        PushVertex(v, f.n * 0.5f + f.u * 0.5f + w * 0.5f, f.n, 1, 1);
        PushVertex(v, f.n * 0.5f - f.u * 0.5f + w * 0.5f, f.n, 0, 1);
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void BuildPlane(std::vector<float>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 up(0, 1, 0);
    PushVertex(v, {-0.5f, 0, -0.5f}, up, 0, 0);
    PushVertex(v, {-0.5f, 0, 0.5f}, up, 0, 1);
    PushVertex(v, {0.5f, 0, 0.5f}, up, 1, 1);
    PushVertex(v, {0.5f, 0, -0.5f}, up, 1, 0);
    idx = {0, 1, 2, 0, 2, 3};
}

void BuildSphere(std::vector<float>& v, std::vector<uint32_t>& idx) {
    constexpr int stacks = 16;
    constexpr int slices = 32;
    for (int i = 0; i <= stacks; ++i) {
        const float theta = kPi * static_cast<float>(i) / stacks;
        for (int j = 0; j <= slices; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
            const glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            PushVertex(v, n * 0.5f, n, static_cast<float>(j) / slices, 1.0f - static_cast<float>(i) / stacks);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
            const uint32_t b = a + slices + 1;
            idx.insert(idx.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
}


// Unit-sized round solids: 1 high, 1 across, centered on the origin.
void BuildCylinder(std::vector<float>& v, std::vector<uint32_t>& idx) {
    constexpr int slices = 40;
    for (int j = 0; j <= slices; ++j) { // side: a ring of top vertices, then a ring of bottom vertices
        const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
        const glm::vec3 n(std::cos(phi), 0.0f, std::sin(phi));
        PushVertex(v, n * 0.5f + glm::vec3(0, 0.5f, 0), n, static_cast<float>(j) / slices, 0.0f);
    }
    for (int j = 0; j <= slices; ++j) {
        const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
        const glm::vec3 n(std::cos(phi), 0.0f, std::sin(phi));
        PushVertex(v, n * 0.5f - glm::vec3(0, 0.5f, 0), n, static_cast<float>(j) / slices, 1.0f);
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t a = static_cast<uint32_t>(j), b = a + slices + 1;
        idx.insert(idx.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
    for (const float side : {1.0f, -1.0f}) { // caps: a center vertex and a ring each
        const uint32_t center = static_cast<uint32_t>(v.size() / 8);
        PushVertex(v, glm::vec3(0, 0.5f * side, 0), glm::vec3(0, side, 0), 0.5f, 0.5f);
        for (int j = 0; j <= slices; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
            PushVertex(v, glm::vec3(0.5f * std::cos(phi), 0.5f * side, 0.5f * std::sin(phi)), glm::vec3(0, side, 0),
                       0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi));
        }
        for (int j = 0; j < slices; ++j) {
            const uint32_t r = center + 1 + static_cast<uint32_t>(j);
            if (side > 0) idx.insert(idx.end(), {center, r + 1, r});
            else idx.insert(idx.end(), {center, r, r + 1});
        }
    }
}

void BuildCone(std::vector<float>& v, std::vector<uint32_t>& idx) {
    constexpr int slices = 40;
    for (int j = 0; j <= slices; ++j) { // side: an apex vertex per slice (smooth normals), then the base ring
        const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
        const glm::vec3 n = glm::normalize(glm::vec3(std::cos(phi), 0.5f, std::sin(phi)));
        PushVertex(v, glm::vec3(0, 0.5f, 0), n, static_cast<float>(j) / slices, 0.0f);
    }
    for (int j = 0; j <= slices; ++j) {
        const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
        const glm::vec3 n = glm::normalize(glm::vec3(std::cos(phi), 0.5f, std::sin(phi)));
        PushVertex(v, glm::vec3(0.5f * std::cos(phi), -0.5f, 0.5f * std::sin(phi)), n, static_cast<float>(j) / slices, 1.0f);
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t a = static_cast<uint32_t>(j), b = a + slices + 1;
        idx.insert(idx.end(), {a + 1, b + 1, b});
    }
    const uint32_t center = static_cast<uint32_t>(v.size() / 8); // the base cap
    PushVertex(v, glm::vec3(0, -0.5f, 0), glm::vec3(0, -1, 0), 0.5f, 0.5f);
    for (int j = 0; j <= slices; ++j) {
        const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
        PushVertex(v, glm::vec3(0.5f * std::cos(phi), -0.5f, 0.5f * std::sin(phi)), glm::vec3(0, -1, 0),
                   0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi));
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t r = center + 1 + static_cast<uint32_t>(j);
        idx.insert(idx.end(), {center, r, r + 1});
    }
}

} // namespace

Mesh::~Mesh() {
    Destroy();
}

void Mesh::Create(const std::vector<float>& vertices, const std::vector<uint32_t>& indices, uint32_t glMode) {
    Destroy();
    m_mode = glMode;
    m_indexCount = static_cast<int>(indices.size());

    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), 0);
    glCreateBuffers(1, &m_ebo);
    glNamedBufferStorage(m_ebo, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);

    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, 8 * sizeof(float));
    glVertexArrayElementBuffer(m_vao, m_ebo);
    const struct { GLuint loc; GLint size; GLuint offset; } attribs[] = {{0, 3, 0}, {1, 3, 12}, {2, 2, 24}};
    for (const auto& a : attribs) {
        glEnableVertexArrayAttrib(m_vao, a.loc);
        glVertexArrayAttribFormat(m_vao, a.loc, a.size, GL_FLOAT, GL_FALSE, a.offset);
        glVertexArrayAttribBinding(m_vao, a.loc, 0);
    }

    // Per-instance attributes (MeshInstance) on binding 1, advanced once per instance. Only instanced draws bind them.
    glVertexArrayBindingDivisor(m_vao, 1, 1);
    for (GLuint i = 0; i < 4; ++i) { // model: four columns, locations 9..12
        glEnableVertexArrayAttrib(m_vao, 9 + i);
        glVertexArrayAttribFormat(m_vao, 9 + i, 4, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(MeshInstance, model) + 16 * i));
        glVertexArrayAttribBinding(m_vao, 9 + i, 1);
    }
    for (GLuint i = 0; i < 3; ++i) { // normal matrix: three columns, padded to four floats each, locations 13..15
        glEnableVertexArrayAttrib(m_vao, 13 + i);
        glVertexArrayAttribFormat(m_vao, 13 + i, 3, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(MeshInstance, normal) + 16 * i));
        glVertexArrayAttribBinding(m_vao, 13 + i, 1);
    }
}

void Mesh::BuildPrimitive(Primitive primitive) {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    switch (primitive) {
    case Primitive::Cube: BuildCube(v, idx); break;
    case Primitive::Plane: BuildPlane(v, idx); break;
    case Primitive::Sphere: BuildSphere(v, idx); break;
    case Primitive::Cylinder: BuildCylinder(v, idx); break;
    case Primitive::Cone: BuildCone(v, idx); break;
    default: BuildExtraPrimitive(primitive, v, idx); break;
    }
    Create(v, idx, GL_TRIANGLES);
}

void Mesh::BuildLineBox() {
    std::vector<float> v;
    for (int i = 0; i < 8; ++i) {
        PushVertex(v, {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f}, {0, 1, 0}, 0, 0);
    }
    const std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7};
    Create(v, idx, GL_LINES);
}

void Mesh::Destroy() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    m_vao = m_vbo = m_ebo = 0;
    m_indexCount = 0;
}

void Mesh::Draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(m_mode, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void Mesh::DrawInstanced(uint32_t instanceBuffer, int count) const {
    glVertexArrayVertexBuffer(m_vao, 1, instanceBuffer, 0, sizeof(MeshInstance));
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(m_mode, m_indexCount, GL_UNSIGNED_INT, nullptr, count);
}

} // namespace magnum
