#version 410 core

in  float vDist;
out vec4  fragColor;

uniform float uFadeRadius; // grid fades to transparent at this distance

void main() {
    float alpha = 1.0 - smoothstep(uFadeRadius * 0.5, uFadeRadius, vDist);
    fragColor = vec4(0.4, 0.4, 0.45, alpha * 0.6);
}
