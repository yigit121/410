#include "Renderer.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

static constexpr int MAX_BONES = 128;

Renderer::~Renderer() {
    for (auto& m : meshes_) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
    }
    if (ubo_)     glDeleteBuffers(1, &ubo_);
    if (boneVao_) glDeleteVertexArrays(1, &boneVao_);
    if (boneVbo_) glDeleteBuffers(1, &boneVbo_);
    if (gridVao_) glDeleteVertexArrays(1, &gridVao_);
    if (gridVbo_) glDeleteBuffers(1, &gridVbo_);
}

void Renderer::initUBO() {
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    // Allocate space for MAX_BONES mat4s (std140: each mat4 = 64 bytes)
    glBufferData(GL_UNIFORM_BUFFER, MAX_BONES * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);  // binding point 0
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Renderer::uploadModel(const Model& model) {
    // Clean up existing
    for (auto& m : meshes_) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
    }
    meshes_.clear();

    for (const Mesh& src : model.meshes) {
        MeshGPU gpu;
        gpu.indexCount = (int)src.indices.size();
        gpu.albedo     = src.albedoTexture;

        glGenVertexArrays(1, &gpu.vao);
        glGenBuffers(1, &gpu.vbo);
        glGenBuffers(1, &gpu.ibo);

        glBindVertexArray(gpu.vao);

        // Upload vertex data
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     src.vertices.size() * sizeof(Vertex),
                     src.vertices.data(), GL_STATIC_DRAW);

        // Upload index data
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     src.indices.size() * sizeof(unsigned int),
                     src.indices.data(), GL_STATIC_DRAW);

        // Attrib 0: position (vec3)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, pos));
        // Attrib 1: normal (vec3)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, nrm));
        // Attrib 2: UV (vec2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, uv));
        // Attrib 3: bone indices (ivec4)
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_INT, sizeof(Vertex),
                               (void*)offsetof(Vertex, bones));
        // Attrib 4: bone weights (vec4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, weights));

        glBindVertexArray(0);
        meshes_.push_back(gpu);
    }

    if (!ubo_) initUBO();

    // Init bone debug VAO (dynamic, updated each frame)
    if (!boneVao_) {
        glGenVertexArrays(1, &boneVao_);
        glGenBuffers(1, &boneVbo_);
        glBindVertexArray(boneVao_);
        glBindBuffer(GL_ARRAY_BUFFER, boneVbo_);
        // Reserve space for up to MAX_BONES*2 vec3 line endpoints
        glBufferData(GL_ARRAY_BUFFER, MAX_BONES * 2 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glBindVertexArray(0);
    }
}

void Renderer::initGrid(float radius, int steps) {
    std::vector<glm::vec3> verts;
    float step = (2.0f * radius) / (float)steps;
    for (int i = 0; i <= steps; i++) {
        float t = -radius + i * step;
        // Line along X axis
        verts.push_back({-radius, 0.0f,  t});
        verts.push_back({ radius, 0.0f,  t});
        // Line along Z axis
        verts.push_back({t, 0.0f, -radius});
        verts.push_back({t, 0.0f,  radius});
    }
    gridVertCount_ = (int)verts.size();

    glGenVertexArrays(1, &gridVao_);
    glGenBuffers(1, &gridVbo_);
    glBindVertexArray(gridVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
}

void Renderer::drawGrid(const Shader& shader, float radius, int steps) {
    if (!gridVao_) initGrid(radius, steps);
    shader.use();
    shader.setFloat("uFadeRadius", radius);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_LINES, 0, gridVertCount_);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void Renderer::uploadSkinningMatrices(const std::vector<glm::mat4>& matrices) {
    if (!ubo_) return;
    int count = std::min((int)matrices.size(), MAX_BONES);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, count * sizeof(glm::mat4), matrices.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Renderer::drawSkinned(const Shader& shader) {
    shader.use();
    for (const MeshGPU& m : meshes_) {
        if (m.albedo >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.albedo);
            shader.setInt("uAlbedo", 0);
        }
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void Renderer::drawBones(const Shader& shader,
                          const std::vector<glm::mat4>& global,
                          const std::vector<Bone>& skeleton) {
    std::vector<glm::vec3> lines;
    lines.reserve(skeleton.size() * 2);
    for (int i = 0; i < (int)skeleton.size(); i++) {
        if (skeleton[i].parent < 0) continue;
        glm::vec3 child  = glm::vec3(global[i][3]);
        glm::vec3 parent = glm::vec3(global[skeleton[i].parent][3]);
        lines.push_back(parent);
        lines.push_back(child);
    }
    if (lines.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, boneVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, lines.size() * sizeof(glm::vec3), lines.data());

    shader.use();
    // Disable depth test so bones are always visible through the mesh
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(boneVao_);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, (int)lines.size());
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);  // restore
}
