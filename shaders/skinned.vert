#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aBones;
layout(location = 4) in vec4  aWeights;

// std140: 128 mat4s = 8192 bytes
layout(std140) uniform BoneMatrices {
    mat4 bones[128];
};

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    // Weighted skin matrix
    mat4 skin = aWeights.x * bones[aBones.x]
              + aWeights.y * bones[aBones.y]
              + aWeights.z * bones[aBones.z]
              + aWeights.w * bones[aBones.w];

    vec4 skinnedPos = skin * vec4(aPos, 1.0);
    // Inverse-transpose for correct normal transform after non-uniform scale
    mat3 nrmMat = mat3(transpose(inverse(skin)));
    vec3 skinnedNrm = normalize(nrmMat * aNrm);

    vec4 worldPos = uModel * skinnedPos;
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(mat3(transpose(inverse(uModel))) * skinnedNrm);
    vUV       = aUV;

    gl_Position = uProjection * uView * worldPos;
}
