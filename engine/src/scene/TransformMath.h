#pragma once

#include "scene/Components.h"

#include <algorithm>
#include <cmath>

namespace magnum {

// Euler angles (degrees; x = pitch, y = yaw, z = roll) that rebuild `rotation` with the same
// Y * X * Z order TransformComponent::RotationMatrix uses. `rotation` has orthonormal columns.
inline glm::vec3 EulerFromRotation(const glm::mat3& r) {
    const float sinPitch = std::clamp(-r[2][1], -1.0f, 1.0f);
    float pitch = std::asin(sinPitch);
    float yaw, roll;
    if (std::abs(sinPitch) > 0.99999f) { // gimbal lock: yaw and roll are the same axis, put it all in yaw
        yaw = std::atan2(-r[0][2], r[0][0]);
        roll = 0.0f;
    } else {
        yaw = std::atan2(r[2][0], r[2][2]);
        roll = std::atan2(r[0][1], r[1][1]);
    }
    return glm::degrees(glm::vec3(pitch, yaw, roll));
}

// Shifts `angle` by multiples of 360 so it lands closest to `previous`. Keeps values from
// jumping between -180 and 180 while an angle is dragged.
inline float UnwrapAngle(float previous, float angle) {
    return angle + 360.0f * std::round((previous - angle) / 360.0f);
}

// Splits a TRS matrix into a TransformComponent. `previous` keeps rotation angles continuous.
inline TransformComponent DecomposeTransform(const glm::mat4& m, const TransformComponent& previous) {
    TransformComponent t;
    t.position = glm::vec3(m[3]);
    t.scale = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
    const glm::mat3 rotation(glm::vec3(m[0]) / std::max(t.scale.x, 1e-8f), glm::vec3(m[1]) / std::max(t.scale.y, 1e-8f),
                             glm::vec3(m[2]) / std::max(t.scale.z, 1e-8f));
    const glm::vec3 euler = EulerFromRotation(rotation);
    t.rotation = {UnwrapAngle(previous.rotation.x, euler.x), UnwrapAngle(previous.rotation.y, euler.y),
                  UnwrapAngle(previous.rotation.z, euler.z)};
    return t;
}

} // namespace magnum
