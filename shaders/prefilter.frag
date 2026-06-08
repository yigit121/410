#version 410 core

// GGX importance-sampled environment prefilter for the specular split-sum.
// Rendered into successive mip levels of the prefiltered cubemap,
// each at a different roughness value.

in  vec3 vDir;
out vec4 fragColor;

uniform samplerCube uEnvMap;
uniform float       uRoughness;

const float PI = 3.14159265359;

// Low-discrepancy sequence (Van der Corput)
float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse(i));
}

// GGX importance-sample H around N
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a  = roughness * roughness;
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // Tangent to world
    vec3 up      = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan   = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

void main() {
    vec3 N = normalize(vDir);
    vec3 V = N; // isotropic: R = V = N

    const uint SAMPLES = 1024u;
    vec3  result      = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = hammersley(i, SAMPLES);
        vec3 H  = importanceSampleGGX(Xi, N, uRoughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            result      += texture(uEnvMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    result = result / max(totalWeight, 1e-4);
    fragColor = vec4(result, 1.0);
}
