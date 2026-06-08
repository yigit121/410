#include "ShadowMap.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

ShadowMap::~ShadowMap() {
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    if (fbo_)      glDeleteFramebuffers(1, &fbo_);
}

void ShadowMap::init(int resolution) {
    res_ = resolution;

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 res_, res_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to a white border so samples outside the map read as "fully lit".
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex_, 0);
    // Depth-only: no color buffer.
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[ShadowMap] FBO incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::beginDepthPass() const {
    glViewport(0, 0, res_, res_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::endDepthPass(int screenW, int screenH) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenW, screenH);
}

glm::mat4 ShadowMap::lightSpaceMatrix(const glm::vec3& center, float radius,
                                      const glm::vec3& toLight) {
    glm::vec3 dir = glm::normalize(toLight);
    glm::vec3 eye = center + dir * (radius * 2.0f);
    // Guard against the light being parallel to the world up vector.
    glm::vec3 up = (glm::abs(dir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius,
                                0.01f, radius * 4.0f);
    return proj * view;
}
