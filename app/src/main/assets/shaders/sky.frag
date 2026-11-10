#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

float cloudField(vec2 p, float cover) {
    float n = fbm(p);
    float lo = 1.0 - cover * 0.95 - 0.02;
    return smoothstep(lo, lo + 0.35, n * 1.15);
}

vec3 aurora(vec3 d, float time) {
    if (d.y < 0.03 || F.w2.w < 0.01) return vec3(0.0);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 12; i++) {
        float fi = float(i);
        float h = (1.2 + fi * 0.22) / (d.y + 0.12);
        vec2 p = d.xz * h;
        float wave = vnoise(vec2(p.x * 0.55 + time * 0.04, fi * 1.7)) * 2.0 + sin(p.x * 0.8 + time * 0.2 + fi) * 0.3;
        float band = exp(-pow((p.y * 0.55 - 1.8 - wave * 0.5) * 1.3, 2.0));
        float rays = 0.5 + 0.5 * vnoise(vec2(p.x * 6.0 + fi * 3.0, time * 0.25 + fi));
        float fade = 1.0 - fi / 12.0;
        vec3 col = mix(vec3(0.15, 1.0, 0.45), vec3(0.45, 0.25, 0.95), smoothstep(0.35, 1.0, fi / 12.0 + 0.2 * vnoise(p * 0.3)));
        acc += col * band * rays * (0.25 + 0.75 * fade) * 0.14;
    }
    return acc * F.w2.w * smoothstep(0.03, 0.25, d.y);
}

void main() {
    vec3 d = normalize(vDir);
    float time = F.camPos.w;
    float night = F.skyAmbient.a;
    vec3 col = skyBase(d);
    float cover = F.w1.x;

    // stars
    if (d.y > -0.02 && night > 0.02) {
        vec3 p = d * 240.0;
        vec3 cell = floor(p);
        vec3 f = fract(p) - 0.5;
        float h = hash13(cell);
        vec3 jit = (vec3(hash13(cell + 7.1), hash13(cell + 13.7), hash13(cell + 3.3)) - 0.5) * 0.7;
        float dist = length(f - jit);
        float bright = step(0.965, h) * smoothstep(0.35, 0.0, dist) * (0.5 + 1.5 * hash13(cell + 1.7));
        bright *= 0.75 + 0.25 * sin(time * (1.0 + 3.0 * hash13(cell)) + h * 40.0);
        // milky way band
        float mw = smoothstep(0.35, 0.0, abs(dot(d, normalize(vec3(0.35, 0.8, -0.5)))));
        float dust = fbm3(d.xz * 9.0 + d.y * 5.0);
        vec3 stars = vec3(1.0, 0.95, 0.9) * bright * 1.6 + vec3(0.35, 0.4, 0.6) * mw * dust * 0.12;
        col += stars * night * (1.0 - cover) * smoothstep(0.0, 0.12, d.y + 0.02);
    }

    // sun disc
    float sd = dot(d, F.sunDir.xyz);
    float sunVis = F.sunColor.a < 0.0 ? 0.0 : 1.0;
    col += F.sunColor.rgb * smoothstep(0.99965, 0.99985, sd) * 12.0 * (1.0 - cover);

    // moon with phase
    vec3 mdir = F.moonDir.xyz;
    float md = dot(d, mdir);
    if (md > 0.996) {
        vec3 up = abs(mdir.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
        vec3 mr = normalize(cross(up, mdir));
        vec3 mu = cross(mdir, mr);
        float R = 0.0235;
        vec2 q = vec2(dot(d, mr), dot(d, mu)) / R;
        float r2 = dot(q, q);
        if (r2 < 1.0) {
            vec3 n = vec3(q, sqrt(1.0 - r2));
            float ph = F.moonDir.w * 2.0 * PI;
            vec3 L = vec3(-sin(ph), 0.15, -cos(ph));
            float lit = smoothstep(-0.04, 0.06, dot(n, normalize(L)));
            float crater = 0.65 + 0.5 * fbm(q * 3.5 + 4.0) - 0.2 * fbm(q * 9.0);
            vec3 mc = vec3(0.93, 0.9, 0.82) * crater;
            col = mix(col, mc * (lit * 2.2 + 0.06), smoothstep(1.0, 0.94, r2) * (1.0 - cover * 0.8));
        }
    }

    col += aurora(d, time) * (1.0 - cover * 0.8) * night;

    // clouds
    if (d.y > 0.0) {
        vec2 wind = vec2(time * 0.006, time * 0.002) * (0.5 + F.w2.x);
        vec2 p = d.xz / (d.y + 0.16) * 0.95 + wind;
        float dens = cloudField(p, cover);
        if (dens > 0.001) {
            vec2 sdir = normalize(F.sunDir.xz + vec2(0.0001));
            float d2 = cloudField(p + sdir * 0.08, cover);
            float shade = clamp(1.0 - (d2 - dens) * 2.0 - dens * 0.35 * (0.5 + cover), 0.0, 1.0);
            vec3 sunlit = F.sunColor.rgb * 0.38 * smoothstep(-0.05, 0.25, F.sunDir.y);
            vec3 amb = F.skyAmbient.rgb * 1.35 + F.skyHorizon.rgb * 0.35;
            vec3 moonl = F.moonColor.rgb * 0.4;
            vec3 cc = amb * (1.0 - 0.45 * cover) + (sunlit + moonl) * shade * (1.0 - 0.55 * cover);
            cc = mix(cc, cc * vec3(0.55, 0.57, 0.62), cover * cover * 0.6);
            float edge = smoothstep(0.0, 0.5, dens);
            col = mix(col, cc, clamp(dens * (0.55 + 0.45 * cover) * smoothstep(0.0, 0.12, d.y), 0.0, 1.0) * edge + (1.0 - edge) * dens * 0.3);
        }
    }

    // ground below horizon fallback (should be covered by terrain)
    if (d.y < 0.0) col = mix(col, F.fogColor.rgb, smoothstep(0.0, -0.1, d.y));

    // lightning flash
    col += vec3(0.7, 0.75, 1.0) * F.w2.y * 1.2 * (0.4 + 0.6 * fbm3(d.xz * 4.0));

    outColor = vec4(aces(col), 1.0);
}
