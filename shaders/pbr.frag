#version 410 core

// Cook-Torrance metallic-roughness BRDF (glTF spec).
// Paired with skinned.vert; receives vNormal, vUV, vWorldPos from the vertex stage.

const float PI = 3.14159265359;

in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

// --- Material textures ---
// Unit 0: base color (sRGB — linearised automatically on sample)
uniform sampler2D uAlbedo;
uniform int       uHasAlbedo;
uniform vec3      uBaseColor;       // baseColorFactor.rgb

// Unit 2: metallic-roughness (linear: G = roughness, B = metallic per glTF spec)
uniform sampler2D uMetallicRoughness;
uniform int       uHasMetallicRoughness;
uniform float     uMetallicFactor;
uniform float     uRoughnessFactor;

// Unit 3: emissive (sRGB)
uniform sampler2D uEmissive;
uniform int       uHasEmissive;
uniform vec3      uEmissiveFactor;

// Unit 4: ambient occlusion (linear, R channel)
uniform sampler2D uOcclusion;
uniform int       uHasOcclusion;

// --- IBL (units 5, 6, 7) ---
uniform samplerCube uIrradianceMap;   // unit 5: diffuse irradiance
uniform samplerCube uPrefilteredMap;  // unit 6: specular prefiltered mip-chain
uniform sampler2D   uBrdfLut;         // unit 7: split-sum scale+bias LUT
uniform int         uIBLEnabled;

// --- Lighting ---
uniform vec3  uLightDir;    // surface -> light, normalized
uniform vec3  uCamPos;
uniform vec3  uLightColor;  // HDR light color; intensity baked in

// --- Shadow (unit 1, bound by App before this draw) ---
uniform sampler2D uShadowMap;
uniform mat4      uLightVP;
uniform int       uShadowEnabled;
uniform float     uShadowBias;

// --- Demo overrides (negative = use material value) ---
uniform float uMetallicOverride;
uniform float uRoughnessOverride;

out vec4 fragColor;

// ── GGX / Smith / Schlick ─────────────────────────────────────────────────────

float D_GGX(float NdotH, float a2) {
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float G_Schlick(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Direct-lighting k = (roughness+1)^2 / 8
float G_Smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return G_Schlick(max(NdotV, 1e-4), k) * G_Schlick(max(NdotL, 1e-4), k);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── 3×3 PCF shadow ────────────────────────────────────────────────────────────

float shadowVisibility(vec3 N, vec3 L) {
    vec4 lp   = uLightVP * vec4(vWorldPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    float bias    = max(uShadowBias * (1.0 - dot(N, L)), uShadowBias * 0.2);
    float current = proj.z - bias;
    float vis     = 0.0;
    vec2  texel   = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            vis += (current > texture(uShadowMap, proj.xy + vec2(x, y) * texel).r)
                   ? 0.0 : 1.0;
    return vis / 9.0;
}

// ── Main ──────────────────────────────────────────────────────────────────────

void main() {
    // Base color (sRGB texture → auto-linearised by GL_SRGB8 upload)
    vec3 albedo = uBaseColor;
    if (uHasAlbedo == 1)
        albedo = texture(uAlbedo, vUV).rgb * uBaseColor;

    // Metallic / roughness
    float metallic  = uMetallicFactor;
    float roughness = uRoughnessFactor;
    if (uHasMetallicRoughness == 1) {
        vec4 mr  = texture(uMetallicRoughness, vUV);
        roughness *= mr.g;   // G = roughness (glTF spec)
        metallic  *= mr.b;   // B = metallic  (glTF spec)
    }
    // Live demo overrides — great for showing the metal/roughness range
    if (uMetallicOverride  >= 0.0) metallic  = uMetallicOverride;
    if (uRoughnessOverride >= 0.0) roughness = uRoughnessOverride;
    roughness = clamp(roughness, 0.05, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);

    // Emissive
    vec3 emissive = uEmissiveFactor;
    if (uHasEmissive == 1)
        emissive *= texture(uEmissive, vUV).rgb;

    // Ambient occlusion
    float ao = 1.0;
    if (uHasOcclusion == 1)
        ao = texture(uOcclusion, vUV).r;

    // Vectors
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = clamp(dot(H, V), 0.0, 1.0);

    // Cook-Torrance specular
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    float a  = roughness * roughness;
    float a2 = a * a;           // perceptual roughness^4 for GGX

    float D  = D_GGX(NdotH, a2);
    float G  = G_Smith(NdotV, NdotL, roughness);
    vec3  F  = F_Schlick(HdotV, F0);

    vec3 kd       = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse  = kd * albedo / PI;
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-3);

    // Direct light contribution
    float shadow = (uShadowEnabled == 1) ? shadowVisibility(N, L) : 1.0;
    vec3  Lo     = (diffuse + specular) * uLightColor * NdotL * shadow;

    // --- Ambient (IBL or constant fallback) ---
    vec3 ambient;
    if (uIBLEnabled == 1) {
        // Fresnel with roughness correction for the ambient term
        vec3 F_ibl = F0 + (max(vec3(1.0 - roughness), F0) - F0)
                       * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);

        // Diffuse irradiance
        vec3 kd_ibl    = (vec3(1.0) - F_ibl) * (1.0 - metallic);
        vec3 diffIBL   = texture(uIrradianceMap, N).rgb * albedo;

        // Specular prefiltered split-sum
        vec3 R         = reflect(-V, N);
        float lod      = roughness * float(5 - 1); // MAX_MIP_LEVELS - 1
        vec3 prefilter = textureLod(uPrefilteredMap, R, lod).rgb;
        vec2 brdf      = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
        vec3 specIBL   = prefilter * (F_ibl * brdf.x + brdf.y);

        ambient = (kd_ibl * diffIBL + specIBL) * ao;
    } else {
        ambient = vec3(0.03) * albedo * ao;
    }

    vec3 color = ambient + Lo + emissive;

    // Reinhard tonemapping — output stays in linear space;
    // GL_FRAMEBUFFER_SRGB (enabled in App::render) applies gamma automatically.
    color = color / (color + vec3(1.0));

    fragColor = vec4(color, 1.0);
}
