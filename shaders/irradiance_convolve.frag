#version 410 core

// Convolves the environment cubemap over the hemisphere to produce
// the diffuse irradiance map (lambertian ambient term).

in  vec3 vDir;
out vec4 fragColor;

uniform samplerCube uEnvMap;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vDir);

    // Build a TBN frame around N
    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = cross(N, right);

    vec3  irradiance  = vec3(0.0);
    float nSamples    = 0.0;
    float sampleDelta = 0.05; // ~3900 samples/hemisphere — fast on modern GPU

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // Tangent-space direction
            vec3 t = vec3(sin(theta) * cos(phi),
                          sin(theta) * sin(phi),
                          cos(theta));
            // To world space
            vec3 sampleVec = t.x * right + t.y * up + t.z * N;
            irradiance += texture(uEnvMap, sampleVec).rgb * cos(theta) * sin(theta);
            nSamples++;
        }
    }

    irradiance = PI * irradiance / nSamples;
    fragColor  = vec4(irradiance, 1.0);
}
