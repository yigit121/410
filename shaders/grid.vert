#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProjection;

out float vDist; // distance from origin, used for fade

void main() {
    vDist = length(aPos.xz);
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
