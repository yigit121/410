#pragma once
#include <glm/glm.hpp>

// Directional-light shadow map: a depth-only FBO rendered from the light's point
// of view. Sampling it in the main pass is a rasterized shadow-ray visibility test.
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    // Allocate the depth FBO + texture (call once, after a GL context exists).
    void init(int resolution = 2048);

    // Bind for the depth pass: sets the FBO, viewport, and clears depth.
    void beginDepthPass() const;
    // Restore the default framebuffer + the given screen viewport.
    static void endDepthPass(int screenW, int screenH);

    // Fit an orthographic light frustum around a world-space bounding sphere
    // (center, radius) looking from direction toLight (surface -> light, normalized).
    static glm::mat4 lightSpaceMatrix(const glm::vec3& center, float radius,
                                      const glm::vec3& toLight);

    unsigned int depthTexture() const { return depthTex_; }
    int          resolution()   const { return res_; }

private:
    unsigned int fbo_      = 0;
    unsigned int depthTex_ = 0;
    int          res_      = 0;
};
