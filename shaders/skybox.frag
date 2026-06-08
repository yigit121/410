#version 410 core

in  vec3 vDir;
out vec4 fragColor;

uniform samplerCube uEnvMap;

void main() {
    vec3 color = texture(uEnvMap, vDir).rgb;
    // Match the Reinhard tonemap used in pbr.frag so the sky and the model
    // sit in the same tone-mapped linear space before GL_FRAMEBUFFER_SRGB.
    color     = color / (color + vec3(1.0));
    fragColor = vec4(color, 1.0);
}
