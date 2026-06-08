#pragma once
#include <glm/glm.hpp>

// Manages IBL (Image-Based Lighting) precomputation.
// Everything runs once at startup via render-to-cubemap (GL 4.1, no compute shaders).
//
// Precomputed outputs:
//   envCubemap_     — 512^2 HDR environment (procedural sky)
//   irradianceMap_  — 32^2  diffuse convolution (Lambertian ambient)
//   prefilteredMap_ — 128^2 mip-chain specular prefilter (GGX importance sampling)
//   brdfLut_        — 512^2 split-sum scale+bias LUT
class Environment {
public:
    static constexpr int MAX_MIP_LEVELS = 5;  // roughness 0..1 across 5 mip levels

    Environment() = default;
    ~Environment();

    // Runs all precompute passes; call once after the GL context is ready
    // and shaders/ is on the working-directory path.
    void init();

    unsigned int envCubemap()     const { return envCubemap_; }
    unsigned int irradianceMap()  const { return irradianceMap_; }
    unsigned int prefilteredMap() const { return prefilteredMap_; }
    unsigned int brdfLut()        const { return brdfLut_; }

    // Draw the unit cube (used at runtime for the skybox).
    void drawCube() const;

private:
    unsigned int envCubemap_     = 0;
    unsigned int irradianceMap_  = 0;
    unsigned int prefilteredMap_ = 0;
    unsigned int brdfLut_        = 0;
    unsigned int captureFBO_     = 0;
    unsigned int cubeVAO_        = 0;
    unsigned int cubeVBO_        = 0;
    unsigned int quadVAO_        = 0;
    unsigned int quadVBO_        = 0;

    void initGeometry();

    unsigned int buildEquirectTexture(int width, int height);
    unsigned int buildEnvCubemap(unsigned int equirectTex);
    void         buildIrradianceMap();
    void         buildPrefilteredMap();
    void         buildBrdfLut();

    // Helpers to set up an FBO for rendering to a specific target
    static void attachCubeFace(unsigned int fbo, unsigned int cubemap, int face, int mip = 0);
};
