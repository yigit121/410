#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ── Vertex layout (interleaved, matches VAO attrib setup) ─────────────────────
struct Vertex {
    glm::vec3 pos;
    glm::vec3 nrm;
    glm::vec2 uv;
    glm::ivec4 bones;   // up to 4 bone indices
    glm::vec4  weights; // corresponding weights (sum = 1.0)
};

// ── Bone ──────────────────────────────────────────────────────────────────────
struct Bone {
    int       parent;       // -1 for root
    glm::mat4 localBind;    // rest-pose local transform
    glm::mat4 invBind;      // inverse bind-pose world transform
    std::string name;
    // Bind-pose TRS components — used for TRS-level animation blending
    glm::vec3 bindT{0.0f, 0.0f, 0.0f};
    glm::quat bindR{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bindS{1.0f, 1.0f, 1.0f};
};

// ── Animation keyframe channel for one bone ───────────────────────────────────
struct Channel {
    int boneIdx;
    std::vector<float>     times;   // keyframe timestamps (seconds)
    std::vector<glm::vec3> T;       // translation keyframes
    std::vector<glm::quat> R;       // rotation keyframes (unit quaternions)
    std::vector<glm::vec3> S;       // scale keyframes
};

// ── Animation clip ─────────────────────────────────────────────────────────────
struct Animation {
    std::string          name;
    float                duration;   // seconds
    std::vector<Channel> channels;   // one per animated bone
};

// ── Mesh primitive ─────────────────────────────────────────────────────────────
struct Mesh {
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    int                       albedoTexture = -1; // GL texture ID, -1 = none
};

// ── Top-level model ─────────────────────────────────────────────────────────────
struct Model {
    std::vector<Mesh>      meshes;
    std::vector<Bone>      skeleton;   // indexed in joint order
    std::vector<Animation> animations;
    glm::mat4              rootTransform{1.0f}; // accumulated scene-node transforms above the skin
};
