#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vDir;

void main() {
    vDir = aPos;
    // Remove translation from view (skybox always surrounds the camera).
    // Set z = w so NDC depth = 1.0 → rendered behind everything.
    vec4 pos    = uProjection * mat4(mat3(uView)) * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
