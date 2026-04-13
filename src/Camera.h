#pragma once
#include <glm/glm.hpp>

// Orbit camera: fixed look-at target, mouse controls yaw/pitch/zoom
class Camera {
public:
    float yaw    =  0.0f;    // degrees, horizontal orbit
    float pitch  = 10.0f;   // degrees, vertical orbit
    float radius =  5.0f;   // distance from target
    glm::vec3 target = {0, 0.9f, 0}; // look-at point (roughly character hip height)

    float fovY  = 45.0f;
    float near_ = 0.05f;
    float far_  = 500.0f;

    glm::mat4 view()                             const;
    glm::mat4 projection(float aspect)           const;
    glm::vec3 position()                         const;

    // Call from GLFW cursor callback
    void onMouseMove(double dx, double dy, bool orbit, bool pan);
    // Call from GLFW scroll callback
    void onScroll(double dy);
};
