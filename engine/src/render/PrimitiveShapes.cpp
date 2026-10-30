#include "render/PrimitiveShapes.h"

#include <glm/glm.hpp>

#include <cmath>

namespace magnum {

namespace {

constexpr float kPi = 3.14159265358979f;

// Collects vertices (position, normal, uv: 8 floats each) and triangles.
class Builder {
public:
    Builder(std::vector<float>& v, std::vector<uint32_t>& idx) : m_v(v), m_idx(idx) {}

    uint32_t Vertex(const glm::vec3& p, const glm::vec3& n, float u, float t) {
        const uint32_t index = static_cast<uint32_t>(m_v.size() / 8);
        m_v.insert(m_v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, t});
        return index;
    }

    // A flat triangle. The winding is chosen so that it faces `outward` (any vector pointing away from the solid).
    void Tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& outward) {
        glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::dot(n, n) < 1e-12f) return; // degenerate
        if (glm::dot(n, outward) < 0.0f) {
            std::swap(b, c);
            n = -n;
        }
        n = glm::normalize(n);
        const uint32_t i = Vertex(a, n, 0, 0);
        Vertex(b, n, 1, 0);
        Vertex(c, n, 0, 1);
        m_idx.insert(m_idx.end(), {i, i + 1, i + 2});
    }

    void Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& outward) {
        Tri(a, b, c, outward);
        Tri(a, c, d, outward);
    }

    void Box(const glm::vec3& lo, const glm::vec3& hi) {
        const glm::vec3 center = (lo + hi) * 0.5f;
        const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}};
        for (const auto& f : faces) {
            const glm::vec3 mid = (c[f[0]] + c[f[1]] + c[f[2]] + c[f[3]]) * 0.25f;
            Quad(c[f[0]], c[f[1]], c[f[2]], c[f[3]], mid - center);
        }
    }

    // A smooth surface on a grid of (rings + 1) x (slices + 1) points. `f(ring, slice)` returns the position and the
    // normal there; each triangle is wound to agree with the normals, so the orientation of the parameters does not matter.
    template <class F>
    void Surface(int rings, int slices, F f) {
        const uint32_t base = static_cast<uint32_t>(m_v.size() / 8);
        std::vector<glm::vec3> pos, nor;
        for (int i = 0; i <= rings; ++i) {
            for (int j = 0; j <= slices; ++j) {
                const auto [p, n] = f(i, j);
                pos.push_back(p);
                nor.push_back(n);
                Vertex(p, n, static_cast<float>(j) / static_cast<float>(slices), static_cast<float>(i) / static_cast<float>(rings));
            }
        }
        auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
            const glm::vec3 g = glm::cross(pos[b] - pos[a], pos[c] - pos[a]);
            if (glm::dot(g, g) < 1e-14f) return; // collapsed at a pole
            if (glm::dot(g, nor[a] + nor[b] + nor[c]) >= 0.0f) m_idx.insert(m_idx.end(), {base + a, base + b, base + c});
            else m_idx.insert(m_idx.end(), {base + a, base + c, base + b});
        };
        for (int i = 0; i < rings; ++i) {
            for (int j = 0; j < slices; ++j) {
                const uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j), b = a + static_cast<uint32_t>(slices + 1);
                emit(a, a + 1, b);
                emit(a + 1, b + 1, b);
            }
        }
    }

private:
    std::vector<float>& m_v;
    std::vector<uint32_t>& m_idx;
};

using PN = std::pair<glm::vec3, glm::vec3>;

glm::vec3 Ring(float radius, float y, float phi) {
    return {radius * std::cos(phi), y, radius * std::sin(phi)};
}

// A flat ring (or disc, innerRadius 0) of triangles at height y.
void FlatRing(Builder& b, float outer, float inner, float y, int slices, const glm::vec3& outward) {
    for (int j = 0; j < slices; ++j) {
        const float p0 = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
        const float p1 = 2.0f * kPi * static_cast<float>(j + 1) / static_cast<float>(slices);
        b.Quad(Ring(outer, y, p0), Ring(outer, y, p1), Ring(inner, y, p1), Ring(inner, y, p0), outward);
    }
}

// An upright cylinder of the given radius and half height, with caps.
void Cylinder(Builder& b, float radius, float halfHeight, int slices) {
    b.Surface(1, slices, [&](int i, int j) -> PN {
        const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
        const glm::vec3 n(std::cos(phi), 0.0f, std::sin(phi));
        return {Ring(radius, i == 0 ? halfHeight : -halfHeight, phi), n};
    });
    FlatRing(b, radius, 0.0f, halfHeight, slices, {0, 1, 0});
    FlatRing(b, radius, 0.0f, -halfHeight, slices, {0, -1, 0});
}

// A hollow cylinder: outer wall, inner wall and two annular ends.
void Tube(Builder& b, float outer, float inner, float halfHeight, int slices) {
    for (const float sign : {1.0f, -1.0f}) { // outer wall faces out, inner wall faces the axis
        const float radius = sign > 0 ? outer : inner;
        b.Surface(1, slices, [&](int i, int j) -> PN {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            return {Ring(radius, i == 0 ? halfHeight : -halfHeight, phi), glm::vec3(std::cos(phi), 0.0f, std::sin(phi)) * sign};
        });
    }
    FlatRing(b, outer, inner, halfHeight, slices, {0, 1, 0});
    FlatRing(b, outer, inner, -halfHeight, slices, {0, -1, 0});
}

} // namespace

bool BuildExtraPrimitive(Primitive primitive, std::vector<float>& v, std::vector<uint32_t>& idx) {
    Builder b(v, idx);
    switch (primitive) {
    case Primitive::Capsule: {
        // Radius 0.25, 1 high: two hemispheres joined by the straight section between the two equator rings.
        constexpr int half = 8, slices = 32;
        b.Surface(2 * half + 1, slices, [&](int i, int j) -> PN {
            const bool top = i <= half;
            const int k = top ? i : i - (half + 1);
            const float theta = (top ? 0.0f : kPi / 2) + (kPi / 2) * static_cast<float>(k) / half;
            const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
            const glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            return {n * 0.25f + glm::vec3(0, top ? 0.25f : -0.25f, 0), n};
        });
        break;
    }
    case Primitive::Hemisphere: {
        constexpr int rings = 12, slices = 32;
        b.Surface(rings, slices, [&](int i, int j) -> PN {
            const float theta = (kPi / 2) * static_cast<float>(i) / rings;
            const float phi = 2.0f * kPi * static_cast<float>(j) / slices;
            const glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            return {n * 0.5f - glm::vec3(0, 0.25f, 0), n};
        });
        FlatRing(b, 0.5f, 0.0f, -0.25f, slices, {0, -1, 0});
        break;
    }
    case Primitive::Torus: {
        constexpr float major = 0.34f, minor = 0.16f;
        constexpr int rings = 20, slices = 40;
        b.Surface(rings, slices, [&](int i, int j) -> PN {
            const float v2 = 2.0f * kPi * static_cast<float>(i) / rings; // around the tube
            const float u = 2.0f * kPi * static_cast<float>(j) / slices; // around the hole
            const glm::vec3 n(std::cos(v2) * std::cos(u), std::sin(v2), std::cos(v2) * std::sin(u));
            return {glm::vec3(major * std::cos(u), 0.0f, major * std::sin(u)) + n * minor, n};
        });
        break;
    }
    case Primitive::Arch: {
        // A half torus standing upright, stretched to 1 high, with flat feet at y = -0.5.
        constexpr float major = 0.34f, minor = 0.16f;
        constexpr int rings = 16, slices = 28;
        auto point = [&](float v2, float u) {
            const float a = major + minor * std::cos(v2);
            return glm::vec3(a * std::cos(u), 2.0f * a * std::sin(u) - 0.5f, minor * std::sin(v2));
        };
        b.Surface(rings, slices, [&](int i, int j) -> PN {
            const float v2 = 2.0f * kPi * static_cast<float>(i) / rings;
            const float u = kPi * static_cast<float>(j) / slices;
            const float e = 1e-3f;
            const glm::vec3 du = point(v2, u + e) - point(v2, u - e);
            const glm::vec3 dv = point(v2 + e, u) - point(v2 - e, u);
            return {point(v2, u), glm::normalize(glm::cross(du, dv))};
        });
        for (const float u : {0.0f, kPi}) { // the two feet
            const glm::vec3 center(major * std::cos(u), -0.5f, 0.0f);
            for (int i = 0; i < rings; ++i) {
                const float v0 = 2.0f * kPi * static_cast<float>(i) / rings, v1 = 2.0f * kPi * static_cast<float>(i + 1) / rings;
                b.Tri(center, point(v0, u), point(v1, u), {0, -1, 0});
            }
        }
        break;
    }
    case Primitive::Pyramid: {
        const glm::vec3 apex(0, 0.5f, 0);
        const glm::vec3 c[4] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}};
        b.Quad(c[0], c[1], c[2], c[3], {0, -1, 0});
        for (int i = 0; i < 4; ++i) b.Tri(c[i], c[(i + 1) % 4], apex, (c[i] + c[(i + 1) % 4] + apex) / 3.0f);
        break;
    }
    case Primitive::Wedge: {
        const glm::vec3 a(-0.5f, -0.5f, -0.5f), bb(0.5f, -0.5f, -0.5f), c(0.5f, -0.5f, 0.5f), d(-0.5f, -0.5f, 0.5f);
        const glm::vec3 e(-0.5f, 0.5f, -0.5f), f(0.5f, 0.5f, -0.5f);
        b.Quad(a, bb, c, d, {0, -1, 0});
        b.Quad(a, bb, f, e, {0, 0, -1});
        b.Quad(d, c, f, e, {0, 1, 1}); // the slope
        b.Tri(a, d, e, {-1, 0, 0});
        b.Tri(bb, c, f, {1, 0, 0});
        break;
    }
    case Primitive::Prism: {
        const glm::vec2 p[3] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}};
        auto at = [&](int i, float z) { return glm::vec3(p[i].x, p[i].y, z); };
        b.Tri(at(0, 0.5f), at(1, 0.5f), at(2, 0.5f), {0, 0, 1});
        b.Tri(at(0, -0.5f), at(1, -0.5f), at(2, -0.5f), {0, 0, -1});
        b.Quad(at(0, 0.5f), at(1, 0.5f), at(1, -0.5f), at(0, -0.5f), {0, -1, 0});
        b.Quad(at(1, 0.5f), at(2, 0.5f), at(2, -0.5f), at(1, -0.5f), {1, 0.5f, 0});
        b.Quad(at(2, 0.5f), at(0, 0.5f), at(0, -0.5f), at(2, -0.5f), {-1, 0.5f, 0});
        break;
    }
    case Primitive::Diamond: {
        const glm::vec3 axis[6] = {{0.5f, 0, 0}, {-0.5f, 0, 0}, {0, 0.5f, 0}, {0, -0.5f, 0}, {0, 0, 0.5f}, {0, 0, -0.5f}};
        for (const int x : {0, 1}) {
            for (const int y : {2, 3}) {
                for (const int z : {4, 5}) b.Tri(axis[x], axis[y], axis[z], axis[x] + axis[y] + axis[z]);
            }
        }
        break;
    }
    case Primitive::Stairs: {
        constexpr int steps = 5;
        for (int i = 0; i < steps; ++i) {
            const float z0 = -0.5f + static_cast<float>(i) / steps;
            b.Box({-0.5f, -0.5f, z0}, {0.5f, -0.5f + static_cast<float>(i + 1) / steps, 0.5f});
        }
        break;
    }
    case Primitive::Quad: {
        const glm::vec3 n(0, 0, 1);
        const uint32_t i = b.Vertex({-0.5f, -0.5f, 0.0f}, n, 0, 0);
        b.Vertex({0.5f, -0.5f, 0.0f}, n, 1, 0);
        b.Vertex({0.5f, 0.5f, 0.0f}, n, 1, 1);
        b.Vertex({-0.5f, 0.5f, 0.0f}, n, 0, 1);
        idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
        break;
    }
    case Primitive::Disc: Cylinder(b, 0.5f, 0.05f, 48); break;
    case Primitive::Tube: Tube(b, 0.5f, 0.36f, 0.5f, 40); break;
    case Primitive::Ring: Tube(b, 0.5f, 0.3f, 0.06f, 48); break;
    default: return false;
    }
    return true;
}

} // namespace magnum
