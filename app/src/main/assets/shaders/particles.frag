#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(push_constant) uniform PC { int type; int n; float a; float b; } pc;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    float r = length(vUV);
    float a;
    if (pc.type == 1) a = smoothstep(1.0, 0.2, abs(vUV.x)) * smoothstep(1.0, 0.7, abs(vUV.y));
    else if (pc.type == 2) a = smoothstep(1.0, 0.0, r) * (0.6 + 0.4 * vnoise(vUV * 3.0 + vColor.rg * 10.0));
    else if (pc.type == 3) a = smoothstep(1.0, 0.7, r);
    else a = smoothstep(1.0, 0.15, r);
    float al = vColor.a * a;
    if (al < 0.003) discard;
    vec3 c = vColor.rgb;
    if (pc.type == 0 || pc.type == 1 || pc.type == 2 || pc.type == 3) c = aces(c);
    outColor = vec4(c, al);
}
