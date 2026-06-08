#version 410 core

in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

uniform vec3 uLightDir;  // surface -> light, normalized
uniform vec3 uCamPos;

// Shadow mapping
uniform sampler2D uShadowMap;
uniform mat4      uLightVP;
uniform int       uShadowEnabled;
uniform float     uShadowBias;

out vec4 fragColor;

// 3x3 PCF shadow lookup. Returns visibility in [0,1] (1 = fully lit).
float shadowVisibility(vec3 N, vec3 L) {
    vec4 lp = uLightVP * vec4(vWorldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;                 // NDC -> [0,1]
    if (proj.z > 1.0) return 1.0;            // beyond far plane: lit

    float bias = max(uShadowBias * (1.0 - dot(N, L)), uShadowBias * 0.2);
    float current = proj.z;

    float vis = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, proj.xy + vec2(x, y) * texel).r;
            vis += (current - bias > closest) ? 0.0 : 1.0;
        }
    return vis / 9.0;
}

void main() {
    // Subtle checkerboard so the shadow reads clearly on the plane.
    vec2 c = floor(vUV * 2.0);
    float checker = mod(c.x + c.y, 2.0);
    vec3 albedo = mix(vec3(0.28, 0.28, 0.32), vec3(0.20, 0.20, 0.24), checker);

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);

    float vis = (uShadowEnabled == 1) ? shadowVisibility(N, L) : 1.0;
    float ambient = 0.25;
    vec3 color = albedo * (ambient + 0.75 * diff * vis);
    fragColor = vec4(color, 1.0);
}
