#version 410 core

// Converts an equirectangular HDR image to one face of a cubemap.

in  vec3 vDir;
out vec4 fragColor;

uniform sampler2D uEquirectMap;

const vec2 kInvAtan = vec2(0.15915494, 0.31830989); // (1/2PI, 1/PI)

vec2 sphericalUV(vec3 v) {
    // atan returns [-PI,PI]; asin returns [-PI/2,PI/2]
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= kInvAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = sphericalUV(normalize(vDir));
    fragColor = vec4(texture(uEquirectMap, uv).rgb, 1.0);
}
