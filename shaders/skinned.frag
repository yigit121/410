#version 410 core

in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

uniform sampler2D uAlbedo;
uniform vec3 uLightDir; // world-space directional light (normalized)
uniform vec3 uCamPos;

out vec4 fragColor;

void main() {
    vec4 albedo = texture(uAlbedo, vUV);
    // Fallback to a neutral grey if no texture
    if (albedo.a < 0.01) albedo = vec4(0.7, 0.7, 0.75, 1.0);

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    float ambient = 0.15;

    vec3 color = albedo.rgb * (ambient + 0.8 * diff) + vec3(0.2) * spec;
    fragColor = vec4(color, 1.0);
}
