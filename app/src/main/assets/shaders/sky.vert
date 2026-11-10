#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec3 vDir;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = p * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    float t = F.camRight.w, a = F.camUp.w;
    vDir = F.camFwd.xyz + F.camRight.xyz * (ndc.x * a * t) - F.camUp.xyz * (ndc.y * t);
}
