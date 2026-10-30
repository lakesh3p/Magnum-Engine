#pragma once

#include "scene/Components.h"

#include <cstdint>
#include <vector>

namespace magnum {

// Builds the geometry of the primitives beyond the basic five (cube, plane, sphere, cylinder, cone): capsule,
// hemisphere, torus, arch, pyramid, wedge, prism, diamond, stairs, disc, tube and ring. Every one fits the unit
// box. Vertices are 8 floats (position, normal, uv). Returns false for a primitive it does not build.
bool BuildExtraPrimitive(Primitive primitive, std::vector<float>& vertices, std::vector<uint32_t>& indices);

} // namespace magnum
