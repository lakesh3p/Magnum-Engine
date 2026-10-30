#pragma once

#include <glm/glm.hpp>

namespace magnum::editor {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};
};

// Free-fly camera. yaw/pitch in degrees; yaw -90 looks down -Z.
class EditorCamera {
public:
    glm::vec3 position{6.0f, 4.5f, 8.0f};
    float yaw = -126.0f;
    float pitch = -24.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 2000.0f;
    float moveSpeed = 6.0f;

    glm::vec3 Forward() const;
    glm::vec3 Right() const;
    glm::mat4 View() const;
    glm::mat4 Projection(float aspect) const;

    void LookAt(const glm::vec3& target);
    // Moves back along the view direction until a sphere of `radius` around `center` fits.
    void Focus(const glm::vec3& center, float radius);
    // ndc in [-1, 1], y up.
    Ray ScreenRay(const glm::vec2& ndc, float aspect) const;
};

} // namespace magnum::editor
