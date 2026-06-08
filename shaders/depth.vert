#version 410 core

// Skinned depth-only pass for shadow mapping. Mirrors skinned.vert's skinning,
// but projects with the light's view-projection and outputs nothing else.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aBones;
layout(location = 4) in vec4  aWeights;

layout(std140) uniform BoneMatrices {
    mat4 bones[128];
};

uniform mat4 uModel;
uniform mat4 uLightVP; // light projection * light view

void main() {
    mat4 skin = aWeights.x * bones[aBones.x]
              + aWeights.y * bones[aBones.y]
              + aWeights.z * bones[aBones.z]
              + aWeights.w * bones[aBones.w];

    vec4 worldPos = uModel * (skin * vec4(aPos, 1.0));
    gl_Position = uLightVP * worldPos;
}
