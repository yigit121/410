#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// Lighting / shadow controls shared between App and the ImGui panel.
struct RenderSettings {
    bool  shadowEnabled = true;
    float shadowBias    = 0.0020f;
    bool  showGround    = true;

    // Directional light intensity (HDR; 1 = radiometrically neutral)
    float lightIntensity = 4.0f;

    // PBR override sliders (negative = use the glTF material value)
    float metallicOverride  = -1.0f;
    float roughnessOverride = -1.0f;

    // IBL
    bool iblEnabled    = true;
    bool skyboxEnabled = true;

    // Directional light orientation, in degrees.
    float lightAzimuth   = 40.0f;    // around Y
    float lightElevation = 55.0f;    // above the horizon

    // Direction from the surface toward the light (normalized).
    glm::vec3 lightDir() const {
        float az = glm::radians(lightAzimuth);
        float el = glm::radians(lightElevation);
        return glm::normalize(glm::vec3(
            std::cos(el) * std::cos(az),
            std::sin(el),
            std::cos(el) * std::sin(az)));
    }
};
