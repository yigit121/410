#version 410 core

// Integrates the Cook-Torrance BRDF over NdotV x roughness, producing
// the split-sum scale+bias LUT used by pbr.frag at runtime.
// Output: R = scale (A term), G = bias (B term).

in  vec2 vUV;
out vec4 fragColor;

const float PI = 3.14159265359;

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

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a  = roughness * roughness;
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3  H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3  up      = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3  tangent = normalize(cross(up, N));
    vec3  bitan   = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

// IBL geometry term uses k = roughness^2/2 (vs direct k=(r+1)^2/8)
float G_schlick(float NdotX, float roughness) {
    float k = (roughness * roughness) / 2.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_smith(float NdotV, float NdotL, float roughness) {
    return G_schlick(NdotV, roughness) * G_schlick(NdotL, roughness);
}

vec2 integrateBRDF(float NdotV, float roughness) {
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0, B = 0.0;
    const uint SAMPLES = 1024u;

    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi  = hammersley(i, SAMPLES);
        vec3 H   = importanceSampleGGX(Xi, N, roughness);
        vec3 L   = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G    = G_smith(max(NdotV, 1e-4), NdotL, roughness);
            float GVis = (G * VdotH) / max(NdotH * NdotV, 1e-4);
            float Fc   = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * GVis;
            B += Fc          * GVis;
        }
    }
    return vec2(A, B) / float(SAMPLES);
}

void main() {
    // UV.x = NdotV [0,1],  UV.y = roughness [0,1]
    vec2 brdf  = integrateBRDF(vUV.x, vUV.y);
    fragColor  = vec4(brdf.x, brdf.y, 0.0, 1.0);
}
