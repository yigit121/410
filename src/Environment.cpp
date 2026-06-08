#include "Environment.h"
#include "Shader.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>
#include <iostream>
#include <algorithm>

// ── Unit-cube geometry (36 verts, positions only) ─────────────────────────────
static const float kCubeVerts[] = {
    // back (-Z)
    -1,-1,-1,  1,1,-1,  1,-1,-1,   1,1,-1, -1,-1,-1, -1,1,-1,
    // front (+Z)
    -1,-1,1,  1,-1,1,  1,1,1,    1,1,1, -1,1,1, -1,-1,1,
    // left (-X)
    -1,1,1, -1,1,-1, -1,-1,-1,   -1,-1,-1, -1,-1,1, -1,1,1,
    // right (+X)
    1,1,1,  1,-1,-1,  1,1,-1,    1,-1,-1,  1,1,1,  1,-1,1,
    // bottom (-Y)
    -1,-1,-1,  1,-1,-1,  1,-1,1,   1,-1,1, -1,-1,1, -1,-1,-1,
    // top (+Y)
    -1,1,-1,  1,1,1,  1,1,-1,    1,1,1, -1,1,-1, -1,1,1,
};

// ── Fullscreen quad (pos2 + uv2) ──────────────────────────────────────────────
static const float kQuadVerts[] = {
    -1,-1, 0,0,   1,-1, 1,0,   1,1, 1,1,
    -1,-1, 0,0,   1, 1, 1,1,  -1,1, 0,1,
};

// ── Capture matrices ──────────────────────────────────────────────────────────
static const glm::mat4 kCaptureProj =
    glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

static const glm::mat4 kCaptureViews[6] = {
    glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)), // +X
    glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)), // -X
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)), // +Y
    glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)), // -Y
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)), // +Z
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)), // -Z
};

// ─────────────────────────────────────────────────────────────────────────────

Environment::~Environment() {
    if (envCubemap_)     glDeleteTextures(1, &envCubemap_);
    if (irradianceMap_)  glDeleteTextures(1, &irradianceMap_);
    if (prefilteredMap_) glDeleteTextures(1, &prefilteredMap_);
    if (brdfLut_)        glDeleteTextures(1, &brdfLut_);
    if (captureFBO_)     glDeleteFramebuffers(1, &captureFBO_);
    if (cubeVAO_)        glDeleteVertexArrays(1, &cubeVAO_);
    if (cubeVBO_)        glDeleteBuffers(1, &cubeVBO_);
    if (quadVAO_)        glDeleteVertexArrays(1, &quadVAO_);
    if (quadVBO_)        glDeleteBuffers(1, &quadVBO_);
}

void Environment::initGeometry() {
    // Cube
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Quad
    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void Environment::drawCube() const {
    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Environment::attachCubeFace(unsigned int fbo, unsigned int cubemap, int face, int mip) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemap, mip);
}

// ── Procedural sky HDR texture ────────────────────────────────────────────────
// Generates a warm afternoon sky (blue zenith, golden horizon, bright sun) as
// a float equirectangular image. No external HDR file required.
unsigned int Environment::buildEquirectTexture(int W, int H) {
    const float PI = 3.14159265f;
    // Sun direction: azimuth 40 deg, elevation 55 deg (matches default light)
    const float sunAz = 40.0f * PI / 180.0f;
    const float sunEl = 55.0f * PI / 180.0f;
    const glm::vec3 sunDir(cosf(sunEl) * cosf(sunAz), sinf(sunEl), cosf(sunEl) * sinf(sunAz));

    std::vector<float> data(W * H * 3);
    for (int j = 0; j < H; j++) {
        for (int i = 0; i < W; i++) {
            float phi  = (float)i / W * 2.0f * PI;
            float elev = ((float)j / H - 0.5f) * PI;  // -PI/2..PI/2

            glm::vec3 dir(cosf(elev) * cosf(phi), sinf(elev), cosf(elev) * sinf(phi));

            glm::vec3 color;
            if (elev >= 0.0f) {
                float t = elev / (PI / 2.0f); // 0=horizon, 1=zenith
                float s = t * t;              // ease-in for more horizon gradient
                glm::vec3 horizon(1.20f, 0.95f, 0.75f);  // warm white horizon
                glm::vec3 zenith (0.12f, 0.30f, 0.70f);  // deep blue sky
                color = glm::mix(horizon, zenith, s);
                // Sun: a sharp bright spot + soft corona glow
                float sd = std::max(0.0f, glm::dot(dir, sunDir));
                color += glm::vec3(10.0f, 8.5f, 5.0f) * powf(sd, 256.0f); // core
                color += glm::vec3(1.8f,  1.2f, 0.5f) * powf(sd,  12.0f); // corona
            } else {
                // Ground: dark earth tone fading from the horizon
                float t = (-elev) / (PI / 2.0f);
                color = glm::mix(glm::vec3(0.22f, 0.18f, 0.14f),
                                 glm::vec3(0.05f, 0.04f, 0.03f), t);
            }

            int idx = (j * W + i) * 3;
            data[idx]   = color.r;
            data[idx+1] = color.g;
            data[idx+2] = color.b;
        }
    }

    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, W, H, 0, GL_RGB, GL_FLOAT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

// ── Shared capture FBO setup ──────────────────────────────────────────────────
// (re-used across all precompute passes)
static void setupCaptureFBO(unsigned int fbo, int size) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // No explicit depth attachment — depth test is disabled during precompute.
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

// ── Equirect → environment cubemap ───────────────────────────────────────────
unsigned int Environment::buildEnvCubemap(unsigned int equirectTex) {
    const int SIZE = 512;

    unsigned int cm;
    glGenTextures(1, &cm);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cm);
    for (int f = 0; f < 6; f++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB16F,
                     SIZE, SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Shader sh("shaders/equirect_to_cube.vert", "shaders/equirect_to_cube.frag");
    sh.use();
    sh.setMat4("uProjection", kCaptureProj);
    sh.setInt("uEquirectMap", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectTex);

    glViewport(0, 0, SIZE, SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    for (int f = 0; f < 6; f++) {
        sh.setMat4("uView", kCaptureViews[f]);
        attachCubeFace(captureFBO_, cm, f);
        glClear(GL_COLOR_BUFFER_BIT);
        drawCube();
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // Generate mipmaps so prefilter LOD sampling works
    glBindTexture(GL_TEXTURE_CUBE_MAP, cm);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    return cm;
}

// ── Diffuse irradiance map ────────────────────────────────────────────────────
void Environment::buildIrradianceMap() {
    const int SIZE = 32;

    glGenTextures(1, &irradianceMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
    for (int f = 0; f < 6; f++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB16F,
                     SIZE, SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Shader sh("shaders/equirect_to_cube.vert", "shaders/irradiance_convolve.frag");
    sh.use();
    sh.setMat4("uProjection", kCaptureProj);
    sh.setInt("uEnvMap", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    glViewport(0, 0, SIZE, SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    for (int f = 0; f < 6; f++) {
        sh.setMat4("uView", kCaptureViews[f]);
        attachCubeFace(captureFBO_, irradianceMap_, f);
        glClear(GL_COLOR_BUFFER_BIT);
        drawCube();
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

// ── Specular prefiltered cubemap (mip chain) ──────────────────────────────────
void Environment::buildPrefilteredMap() {
    const int BASE = 128;

    glGenTextures(1, &prefilteredMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredMap_);
    for (int f = 0; f < 6; f++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB16F,
                     BASE, BASE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // allocate all mip levels

    Shader sh("shaders/equirect_to_cube.vert", "shaders/prefilter.frag");
    sh.use();
    sh.setMat4("uProjection", kCaptureProj);
    sh.setInt("uEnvMap", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    for (int mip = 0; mip < MAX_MIP_LEVELS; mip++) {
        int  mipSize  = BASE >> mip;              // 128, 64, 32, 16, 8
        float roughness = (float)mip / (float)(MAX_MIP_LEVELS - 1);

        glViewport(0, 0, mipSize, mipSize);
        sh.setFloat("uRoughness", roughness);

        for (int f = 0; f < 6; f++) {
            sh.setMat4("uView", kCaptureViews[f]);
            attachCubeFace(captureFBO_, prefilteredMap_, f, mip);
            glClear(GL_COLOR_BUFFER_BIT);
            drawCube();
        }
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

// ── BRDF integration LUT ──────────────────────────────────────────────────────
void Environment::buildBrdfLut() {
    const int SIZE = 512;

    glGenTextures(1, &brdfLut_);
    glBindTexture(GL_TEXTURE_2D, brdfLut_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, brdfLut_, 0);
    glViewport(0, 0, SIZE, SIZE);
    glClear(GL_COLOR_BUFFER_BIT);

    Shader sh("shaders/brdf_lut.vert", "shaders/brdf_lut.frag");
    sh.use();
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ── Public entry point ────────────────────────────────────────────────────────
void Environment::init() {
    std::cout << "[Environment] Running IBL precompute...\n";

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    initGeometry();

    // Single capture FBO (reused by all passes)
    glGenFramebuffers(1, &captureFBO_);

    // Build pipeline
    unsigned int equirectTex = buildEquirectTexture(512, 256);
    envCubemap_ = buildEnvCubemap(equirectTex);
    glDeleteTextures(1, &equirectTex);

    buildIrradianceMap();
    buildPrefilteredMap();
    buildBrdfLut();

    // Restore default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[Environment] IBL ready.\n";
}
