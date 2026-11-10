#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aAttr;
layout(location = 4) in vec4 m0;
layout(location = 5) in vec4 m1;
layout(location = 6) in vec4 m2;
layout(location = 7) in vec4 m3;
layout(location = 8) in vec4 aTint;
layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vN;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vAttr;
layout(location = 4) out vec4 vTint;
void main() {
    mat4 model = mat4(m0, m1, m2, m3);
    vec4 wp = model * vec4(aPos, 1.0);
    vec3 n = normalize(mat3(model) * aNrm);
    float t = F.camPos.w;
    float wind = F.w2.x;
    if (aAttr.z > 0.0) {
        float w = aAttr.z * aAttr.z;
        float ph = t * 1.3 + wp.x * 0.21 + wp.z * 0.17;
        wp.x += (sin(ph) + 0.5 * sin(ph * 2.3 + 1.0)) * wind * 0.22 * w;
        wp.z += cos(ph * 0.9) * wind * 0.12 * w;
    }
    // flag waving
    if (aAttr.x > 104.5 && aAttr.x < 106.5) {
        float k = aUV.x;
        wp.z += sin(t * 5.0 - aUV.x * 7.0) * (0.12 + wind * 0.25) * k;
        wp.y += sin(t * 4.0 - aUV.x * 5.0) * 0.05 * k;
    }
    vPos = wp.xyz;
    vN = n;
    vUV = aUV;
    vAttr = aAttr;
    vTint = aTint;
    gl_Position = F.viewProj * wp;
}
