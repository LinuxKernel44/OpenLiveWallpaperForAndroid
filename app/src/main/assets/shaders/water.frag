#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set = 1, binding = 0) uniform sampler2DArray uAlb;
layout(set = 1, binding = 1) uniform sampler2DArray uNrm;
layout(set = 1, binding = 2) uniform sampler2DShadow uShadow;
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vN;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vAttr;
layout(location = 4) in vec4 vTint;
layout(location = 0) out vec4 outColor;

float waterH(vec2 p) {
    float t = F.camPos.w;
    float flow = F.w3.z;
    vec2 q = vec2(p.x * 2.2, p.y * 0.9 + t * 1.8 * flow);
    float h = vnoise(q * vec2(2.5, 1.0)) * 0.6 + vnoise(q * vec2(6.0, 2.2) + 11.0) * 0.3 + vnoise(q * vec2(13.0, 5.0) - t * 0.4) * 0.15;
    return h * (0.3 + 0.7 * flow);
}

void main() {
    float t = F.camPos.w;
    vec3 V = normalize(F.camPos.xyz - vPos);
    float dist = length(F.camPos.xyz - vPos);
    float flow = F.w3.z;
    float ice = F.w2.z;
    float edge = abs(vUV.x - 0.5) * 2.0;
    vec2 p = vec2(vUV.x, vUV.y);
    float e = 0.04;
    float h0 = waterH(p), hx = waterH(p + vec2(e, 0)), hz = waterH(p + vec2(0, e));
    vec3 N = normalize(vec3(-(hx - h0) / e * 0.045, 1.0, -(hz - h0) / e * 0.06));
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 refl = skyBase(R);
    // dark tree / bank reflection near edges
    refl = mix(refl, refl * vec3(0.25, 0.3, 0.28), smoothstep(0.45, 1.0, edge) * 0.8);
    float sunSpec = pow(max(dot(R, F.sunDir.xyz), 0.0), 900.0) * 30.0 + pow(max(dot(R, F.sunDir.xyz), 0.0), 60.0) * 0.4;
    refl += F.sunColor.rgb * sunSpec * (1.0 - F.w1.x);
    float moonSpec = pow(max(dot(R, F.moonDir.xyz), 0.0), 500.0) * 4.0;
    refl += F.moonColor.rgb * moonSpec;

    vec3 bed = texture(uAlb, vec3(vPos.xz * 0.9 + N.xz * 0.1, 7.0)).rgb;
    float caust = pow(vnoise(vPos.xz * 3.0 + t * 0.6 * flow) * vnoise(vPos.xz * 2.1 - t * 0.5 * flow), 2.0) * 3.0;
    vec3 bedLit = bed * (F.sunColor.rgb * 0.45 * (1.0 + caust) + F.skyAmbient.rgb * 0.8) * vec3(0.35, 0.55, 0.5) * (1.0 - edge * 0.4);
    float fres = 0.03 + 0.97 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 col = mix(bedLit, refl, clamp(fres + 0.1, 0.0, 1.0));

    // foam on edges and rapids
    float fn = vnoise(vec2(vUV.x * 18.0, vUV.y * 3.0 + t * 2.0 * flow));
    float foam = smoothstep(0.72, 1.0, edge + (fn - 0.5) * 0.45) * (0.4 + 0.6 * flow);
    foam += smoothstep(0.82, 0.95, vnoise(vUV * vec2(7.0, 1.8) + vec2(0.0, t * 1.2 * flow))) * 0.25 * flow;
    vec3 foamCol = vec3(0.85, 0.9, 0.95) * (F.sunColor.rgb * 0.5 + F.skyAmbient.rgb * 1.4);
    col = mix(col, foamCol, clamp(foam, 0.0, 1.0) * 0.8);

    // ice + snow
    float frozen = clamp((ice - (1.0 - edge) * 0.8) * 3.0, 0.0, 1.0);
    vec3 iceA = texture(uAlb, vec3(vPos.xz * 0.35, 10.0)).rgb * (F.sunColor.rgb * 0.7 + F.skyAmbient.rgb * 1.4);
    iceA = mix(iceA, refl * 0.7, 0.25);
    vec3 snowA = texture(uAlb, vec3(vPos.xz * 0.3, 0.0)).rgb * (F.sunColor.rgb * 0.8 + F.skyAmbient.rgb * 1.5) * 1.05;
    vec3 frozenCol = mix(iceA, snowA, clamp(F.w0.y * 1.3 - 0.2 + (fn - 0.5) * 0.4, 0.0, 1.0));
    col = mix(col, frozenCol, frozen);

    // night: dim by existing ambient already; add point light glints
    for (int i = 0; i < 12; i++) {
        vec4 lp = F.lightPos[i];
        if (lp.w <= 0.0) continue;
        float r = length(lp.xyz - vPos);
        if (r > lp.w) continue;
        float at = 1.0 - r / lp.w; at *= at;
        col += F.lightCol[i].rgb * F.lightCol[i].a * at * 0.12 * (1.0 - frozen * 0.5);
    }

    // fog
    float fogF = 1.0 - exp(-dist * F.fogColor.a - dist * F.w1.w * 0.012);
    col = mix(col, F.fogColor.rgb, clamp(fogF, 0.0, 1.0));
    float alpha = (1.0 - smoothstep(0.9, 1.0, edge * (1.0 - frozen * 0.0)));
    alpha = mix(alpha, 1.0, frozen * 0.5);
    outColor = vec4(aces(col), alpha);
}
