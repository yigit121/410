#pragma once
#include "Model.h"
#include "Shader.h"
#include <vector>
#include <glm/glm.hpp>

struct MeshGPU {
    unsigned int vao = 0, vbo = 0, ibo = 0;
    int indexCount = 0;
    int albedo     = -1; // GL texture ID
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    void uploadModel(const Model& model);
    void uploadSkinningMatrices(const std::vector<glm::mat4>& matrices);

    // Draw the skinned mesh
    void drawSkinned(const Shader& shader);

    // Draw bone debug lines
    void drawBones(const Shader& shader,
                   const std::vector<glm::mat4>& global,
                   const std::vector<Bone>& skeleton);

    // Draw flat ground grid centred at origin
    void drawGrid(const Shader& shader, float radius = 5.0f, int steps = 20);

    // Returns total triangle count across all uploaded meshes
    int totalTriangles() const {
        int t = 0;
        for (const auto& m : meshes_) t += m.indexCount / 3;
        return t;
    }

private:
    std::vector<MeshGPU> meshes_;
    unsigned int ubo_     = 0;   // Uniform Buffer Object for skinning matrices
    unsigned int boneVao_ = 0, boneVbo_ = 0;
    unsigned int gridVao_ = 0, gridVbo_ = 0;
    int          gridVertCount_ = 0;

    void initUBO();
    void initGrid(float radius, int steps);
};
