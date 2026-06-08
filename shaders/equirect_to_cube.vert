#version 410 core

// Shared vertex shader for all IBL precompute passes.
// Renders a unit cube; the local position becomes the sample direction.

layout(location = 0) in vec3 aPos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vDir;

void main() {
    vDir = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
