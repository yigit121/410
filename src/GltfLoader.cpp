#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "GltfLoader.h"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Read a typed accessor into a flat vector<T>
template<typename T>
static std::vector<T> readAccessor(const tinygltf::Model& g, int accIdx) {
    const auto& acc  = g.accessors[accIdx];
    const auto& view = g.bufferViews[acc.bufferView];
    const auto& buf  = g.buffers[view.buffer];

    int stride = view.byteStride ? (int)view.byteStride : (int)sizeof(T);
    const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;

    std::vector<T> out(acc.count);
    for (size_t i = 0; i < acc.count; i++)
        memcpy(&out[i], base + i * stride, sizeof(T));
    return out;
}

// Upload an image to a GL texture
static int uploadTexture(const tinygltf::Model& g, int texIdx) {
    if (texIdx < 0) return -1;
    const tinygltf::Texture& t = g.textures[texIdx];
    const tinygltf::Image&   img = g.images[t.source];

    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    GLenum fmt = img.component == 4 ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt,
                 img.width, img.height, 0, fmt, GL_UNSIGNED_BYTE, img.image.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return (int)id;
}

// Decompose a glTF node into a mat4 local transform
static glm::mat4 nodeLocalTransform(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        glm::mat4 m;
        for (int i = 0; i < 16; i++) ((float*)glm::value_ptr(m))[i] = (float)node.matrix[i];
        return m;
    }
    glm::vec3 T(0), S(1);
    glm::quat R(1,0,0,0);
    if (node.translation.size() == 3)
        T = {(float)node.translation[0], (float)node.translation[1], (float)node.translation[2]};
    if (node.rotation.size() == 4)
        R = glm::quat((float)node.rotation[3], (float)node.rotation[0],
                      (float)node.rotation[1], (float)node.rotation[2]);
    if (node.scale.size() == 3)
        S = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};

    return glm::translate(glm::mat4(1), T) * glm::mat4_cast(R) * glm::scale(glm::mat4(1), S);
}

// ── Main loader ───────────────────────────────────────────────────────────────

Model GltfLoader::load(const std::string& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model    g;
    std::string err, warn;

    bool ok = path.size() >= 4 && path.substr(path.size()-4) == ".glb"
        ? loader.LoadBinaryFromFile(&g, &err, &warn, path)
        : loader.LoadASCIIFromFile (&g, &err, &warn, path);

    if (!warn.empty()) std::cerr << "[tinygltf] warn: " << warn << "\n";
    if (!ok)           throw std::runtime_error("[tinygltf] " + err);

    Model model;

    // ── 1. Skeleton (from first skin) ─────────────────────────────────────────
    if (!g.skins.empty()) {
        const tinygltf::Skin& skin = g.skins[0];
        int nBones = (int)skin.joints.size();
        model.skeleton.resize(nBones);

        // Map glTF node index -> joint/bone index
        std::unordered_map<int,int> nodeToJoint;
        for (int j = 0; j < nBones; j++)
            nodeToJoint[skin.joints[j]] = j;

        // Inverse bind matrices
        std::vector<glm::mat4> ibms(nBones, glm::mat4(1.0f));
        if (skin.inverseBindMatrices >= 0) {
            const auto& acc  = g.accessors[skin.inverseBindMatrices];
            const auto& view = g.bufferViews[acc.bufferView];
            const auto& buf  = g.buffers[view.buffer];
            const float* ptr = (const float*)(buf.data.data() + view.byteOffset + acc.byteOffset);
            for (int j = 0; j < nBones; j++)
                memcpy(glm::value_ptr(ibms[j]), ptr + j*16, 64);
        }

        for (int j = 0; j < nBones; j++) {
            int nodeIdx = skin.joints[j];
            const tinygltf::Node& node = g.nodes[nodeIdx];
            model.skeleton[j].name      = node.name;
            model.skeleton[j].invBind   = ibms[j];
            model.skeleton[j].localBind = nodeLocalTransform(node);
            model.skeleton[j].parent    = -1;

            // Store bind-pose TRS for animation blending
            {
                glm::vec3 bT(0.0f), bS(1.0f);
                glm::quat bR(1.0f, 0.0f, 0.0f, 0.0f);
                if (!node.matrix.empty()) {
                    // Decompose the pre-built mat4
                    glm::vec3 skew; glm::vec4 persp;
                    glm::decompose(model.skeleton[j].localBind, bS, bR, bT, skew, persp);
                    bR = glm::conjugate(bR); // GLM decompose returns conjugate rotation
                } else {
                    if (node.translation.size() == 3)
                        bT = { (float)node.translation[0], (float)node.translation[1], (float)node.translation[2] };
                    if (node.rotation.size() == 4)
                        bR = glm::quat((float)node.rotation[3], (float)node.rotation[0],
                                       (float)node.rotation[1], (float)node.rotation[2]);
                    if (node.scale.size() == 3)
                        bS = { (float)node.scale[0], (float)node.scale[1], (float)node.scale[2] };
                }
                model.skeleton[j].bindT = bT;
                model.skeleton[j].bindR = bR;
                model.skeleton[j].bindS = bS;
            }

            // Find parent: scan other joints to find who has nodeIdx as a child
            for (int k = 0; k < nBones; k++) {
                const tinygltf::Node& pn = g.nodes[skin.joints[k]];
                for (int child : pn.children) {
                    if (child == nodeIdx) {
                        model.skeleton[j].parent = k;
                        break;
                    }
                }
                if (model.skeleton[j].parent >= 0) break;
            }
        }

        // ── 1b. Root transform — accumulate scene-node transforms above the skin ──
        // Walk the scene hierarchy; multiply transforms of any node that is NOT
        // one of the skin's joints, stopping once we reach a joint node.
        {
            std::unordered_set<int> jointSet(skin.joints.begin(), skin.joints.end());
            // DFS from each scene root; accumulate until we hit a joint
            std::function<bool(int, glm::mat4)> walk = [&](int nodeIdx, glm::mat4 acc) -> bool {
                if (jointSet.count(nodeIdx)) {
                    model.rootTransform = acc;
                    return true; // found
                }
                glm::mat4 local = nodeLocalTransform(g.nodes[nodeIdx]);
                for (int child : g.nodes[nodeIdx].children)
                    if (walk(child, acc * local)) return true;
                return false;
            };
            if (!g.scenes.empty())
                for (int root : g.scenes[g.defaultScene >= 0 ? g.defaultScene : 0].nodes)
                    if (walk(root, glm::mat4(1.0f))) break;
        }

        // ── 2. Meshes ─────────────────────────────────────────────────────────
        for (const tinygltf::Node& node : g.nodes) {
            if (node.mesh < 0) continue;
            const tinygltf::Mesh& gMesh = g.meshes[node.mesh];

            for (const tinygltf::Primitive& prim : gMesh.primitives) {
                if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

                Mesh mesh;

                // Positions
                std::vector<glm::vec3> positions;
                if (prim.attributes.count("POSITION"))
                    positions = readAccessor<glm::vec3>(g, prim.attributes.at("POSITION"));

                // Normals
                std::vector<glm::vec3> normals(positions.size(), glm::vec3(0,1,0));
                if (prim.attributes.count("NORMAL"))
                    normals = readAccessor<glm::vec3>(g, prim.attributes.at("NORMAL"));

                // UVs
                std::vector<glm::vec2> uvs(positions.size(), glm::vec2(0));
                if (prim.attributes.count("TEXCOORD_0"))
                    uvs = readAccessor<glm::vec2>(g, prim.attributes.at("TEXCOORD_0"));

                // Bone indices (JOINTS_0) — stored as unsigned short or ubyte
                std::vector<glm::ivec4> boneIdx(positions.size(), glm::ivec4(0));
                if (prim.attributes.count("JOINTS_0")) {
                    const auto& acc = g.accessors[prim.attributes.at("JOINTS_0")];
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto raw = readAccessor<glm::u16vec4>(g, prim.attributes.at("JOINTS_0"));
                        for (size_t i = 0; i < raw.size(); i++)
                            boneIdx[i] = {raw[i].x, raw[i].y, raw[i].z, raw[i].w};
                    } else {
                        auto raw = readAccessor<glm::u8vec4>(g, prim.attributes.at("JOINTS_0"));
                        for (size_t i = 0; i < raw.size(); i++)
                            boneIdx[i] = {raw[i].x, raw[i].y, raw[i].z, raw[i].w};
                    }
                }

                // Weights
                std::vector<glm::vec4> weights(positions.size(), glm::vec4(1,0,0,0));
                if (prim.attributes.count("WEIGHTS_0"))
                    weights = readAccessor<glm::vec4>(g, prim.attributes.at("WEIGHTS_0"));

                // Assemble interleaved vertices
                mesh.vertices.resize(positions.size());
                for (size_t i = 0; i < positions.size(); i++) {
                    mesh.vertices[i] = {
                        positions[i], normals[i], uvs[i],
                        boneIdx[i], weights[i]
                    };
                }

                // Indices — typically unsigned short; handle unsigned int too
                if (prim.indices >= 0) {
                    const auto& acc = g.accessors[prim.indices];
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto raw = readAccessor<unsigned short>(g, prim.indices);
                        mesh.indices.assign(raw.begin(), raw.end());
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        mesh.indices = readAccessor<unsigned int>(g, prim.indices);
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        auto raw = readAccessor<unsigned char>(g, prim.indices);
                        mesh.indices.assign(raw.begin(), raw.end());
                    }
                }

                // Texture
                if (prim.material >= 0) {
                    const auto& mat = g.materials[prim.material];
                    int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                    mesh.albedoTexture = uploadTexture(g, texIdx);
                }

                model.meshes.push_back(std::move(mesh));
            }
        }

        // ── 3. Animations ─────────────────────────────────────────────────────
        for (const tinygltf::Animation& ga : g.animations) {
            Animation anim;
            anim.name = ga.name;
            anim.duration = 0.0f;

            for (const tinygltf::AnimationChannel& gc : ga.channels) {
                if (gc.target_node < 0 || !nodeToJoint.count(gc.target_node)) continue;
                int boneIdx = nodeToJoint.at(gc.target_node);

                const tinygltf::AnimationSampler& sampler = ga.samplers[gc.sampler];
                std::vector<float> times = readAccessor<float>(g, sampler.input);
                if (!times.empty()) anim.duration = std::max(anim.duration, times.back());

                // Find or create channel for this bone
                Channel* ch = nullptr;
                for (auto& c : anim.channels)
                    if (c.boneIdx == boneIdx) { ch = &c; break; }
                if (!ch) {
                    anim.channels.push_back({boneIdx, {}, {}, {}, {}});
                    ch = &anim.channels.back();
                }

                const std::string& path = gc.target_path;
                if (path == "translation") {
                    ch->times = times;
                    ch->T = readAccessor<glm::vec3>(g, sampler.output);
                } else if (path == "rotation") {
                    ch->times = times;
                    auto raw = readAccessor<glm::vec4>(g, sampler.output); // xyzw
                    ch->R.reserve(raw.size());
                    for (auto& v : raw)
                        ch->R.push_back(glm::quat(v.w, v.x, v.y, v.z)); // GLM: wxyz
                } else if (path == "scale") {
                    ch->times = times;
                    ch->S = readAccessor<glm::vec3>(g, sampler.output);
                }
            }
            model.animations.push_back(std::move(anim));
        }
    } else {
        // No skin — load static mesh only
        for (const tinygltf::Mesh& gMesh : g.meshes) {
            for (const tinygltf::Primitive& prim : gMesh.primitives) {
                if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
                Mesh mesh;
                if (prim.attributes.count("POSITION"))
                    for (auto& p : readAccessor<glm::vec3>(g, prim.attributes.at("POSITION")))
                        mesh.vertices.push_back({p, {0,1,0}, {0,0}, {0,0,0,0}, {1,0,0,0}});
                if (prim.indices >= 0) {
                    const auto& acc = g.accessors[prim.indices];
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto raw = readAccessor<unsigned short>(g, prim.indices);
                        mesh.indices.assign(raw.begin(), raw.end());
                    } else {
                        mesh.indices = readAccessor<unsigned int>(g, prim.indices);
                    }
                }
                model.meshes.push_back(std::move(mesh));
            }
        }
    }

    // ── Diagnostic output ──────────────────────────────────────────────────────
    std::cout << "[GltfLoader] Loaded: " << path << "\n"
              << "  Bones:   " << model.skeleton.size()   << "\n"
              << "  Meshes:  " << model.meshes.size()     << "\n"
              << "  Clips:   " << model.animations.size() << "\n";
    for (auto& a : model.animations)
        std::cout << "    clip \"" << a.name << "\" duration=" << a.duration << "s\n";

    return model;
}
