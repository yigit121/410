#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

static constexpr float DEG2RAD = 3.14159265f / 180.0f;

glm::vec3 Camera::position() const {
    float p = pitch * DEG2RAD;
    float y = yaw   * DEG2RAD;
    return target + glm::vec3(
        radius * cosf(p) * sinf(y),
        radius * sinf(p),
        radius * cosf(p) * cosf(y)
    );
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position(), target, glm::vec3(0,1,0));
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(fovY), aspect, near_, far_);
}

void Camera::onMouseMove(double dx, double dy, bool orbit, bool pan) {
    if (orbit) {
        yaw   += (float)dx * 0.4f;
        pitch += (float)dy * 0.4f;
        pitch  = std::clamp(pitch, -89.0f, 89.0f);
    }
    if (pan) {
        // Compute camera right and up vectors for panning in view plane
        glm::vec3 fwd  = glm::normalize(target - position());
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
        glm::vec3 up    = glm::cross(right, fwd);
        float scale = radius * 0.001f;
        target -= right * (float)dx * scale;
        target += up    * (float)dy * scale;
    }
}

void Camera::onScroll(double dy) {
    radius *= (1.0f - (float)dy * 0.1f);
    radius  = std::clamp(radius, 0.2f, 200.0f);
}
