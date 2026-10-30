#include "EditorCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace magnum::editor {

glm::vec3 EditorCamera::Forward() const {
    const float y = glm::radians(yaw);
    const float p = glm::radians(pitch);
    return glm::normalize(glm::vec3(std::cos(p) * std::cos(y), std::sin(p), std::cos(p) * std::sin(y)));
}

glm::vec3 EditorCamera::Right() const {
    return glm::normalize(glm::cross(Forward(), glm::vec3(0, 1, 0)));
}

glm::mat4 EditorCamera::View() const {
    return glm::lookAt(position, position + Forward(), glm::vec3(0, 1, 0));
}

glm::mat4 EditorCamera::Projection(float aspect) const {
    return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
}

void EditorCamera::LookAt(const glm::vec3& target) {
    const glm::vec3 d = glm::normalize(target - position);
    yaw = glm::degrees(std::atan2(d.z, d.x));
    pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
}

void EditorCamera::Focus(const glm::vec3& center, float radius) {
    const float distance = std::max(radius, 0.1f) / std::sin(glm::radians(fovDegrees) * 0.5f);
    position = center - Forward() * distance;
}

Ray EditorCamera::ScreenRay(const glm::vec2& ndc, float aspect) const {
    const glm::mat4 inv = glm::inverse(Projection(aspect) * View());
    glm::vec4 nearPoint = inv * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = inv * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return {glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint))};
}

} // namespace magnum::editor
