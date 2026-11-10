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

vec3 sAlb(int l, vec2 uv) { return texture(uAlb, vec3(uv, float(l))).rgb; }
vec4 sAlb4(int l, vec2 uv) { return texture(uAlb, vec3(uv, float(l))); }
vec3 sNrm(int l, vec2 uv) { return texture(uNrm, vec3(uv, float(l))).xyz * 2.0 - 1.0; }

vec3 perturb(vec3 N, vec3 p, vec2 uv, vec3 tn, float strength) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    mat3 TBN = mat3(T * invmax, B * invmax, N);
    tn.xy *= strength;
    return normalize(TBN * tn);
}

float shadowAt(vec3 p, vec3 N) {
    vec4 lp = F.lightVP * vec4(p + N * 0.12, 1.0);
    vec3 sc = lp.xyz;
    sc.xy = sc.xy * 0.5 + 0.5;
    if (sc.z > 1.0 || sc.z < 0.0) return 1.0;
    float edge = smoothstep(0.0, 0.07, min(min(sc.x, 1.0 - sc.x), min(sc.y, 1.0 - sc.y)));
    if (edge <= 0.0) return 1.0;
    float ts = 1.0 / 2048.0;
    float s = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            s += texture(uShadow, vec3(sc.xy + vec2(x, y) * ts * 1.3, sc.z - 0.0012));
    return mix(1.0, s / 9.0, edge);
}

struct Surf { vec3 alb; vec3 N; float rough; float spec; float emis; float snow; };

float snowMask(vec3 N, float ao, float affinity, vec3 p, float coverage) {
    float n = vnoise(p.xz * 0.9 + p.y * 0.3);
    float up = N.y + (n - 0.5) * 0.35;
    float m = smoothstep(0.28, 0.72, up) * mix(0.55, 1.0, affinity);
    m *= smoothstep(0.25, 0.7, ao);
    return clamp(m * coverage * 1.6 - (1.0 - coverage) * 0.4 * (1.0 - n), 0.0, 1.0) * step(0.001, coverage);
}

Surf terrainSurf(vec3 p, vec3 Ng, float forest, float wet, float dist) {
    Surf s;
    float season = F.w0.x;
    float snowCover = F.w0.y;
    float autumn = F.w0.z;
    float green = F.w0.w;
    vec2 uv = p.xz;
    float slope = 1.0 - Ng.y;
    float macro = 0.6 + 0.8 * fbm3(p.xz * 0.05 + 11.0);
    float near = 1.0 - smoothstep(60.0, 300.0, dist);

    float rockW = smoothstep(0.22, 0.42, slope + (fbm3(p.xz * 0.2) - 0.5) * 0.15);
    float altRock = smoothstep(95.0, 190.0, p.y);
    rockW = max(rockW, altRock * 0.8);
    float gravelW = smoothstep(0.55, 0.9, wet);
    float forestW = smoothstep(0.35, 0.8, forest);

    vec3 grassA = sAlb(1, uv * 0.45);
    vec3 dry = vec3(0.50, 0.42, 0.26) * (0.6 + grassA.g);
    float gpatch = fbm3(p.xz * 0.35);
    vec3 grassG = grassA * mix(vec3(0.55, 0.66, 0.3), vec3(0.78, 0.8, 0.38), gpatch);
    vec3 grassC = mix(dry, grassG, green);
    grassC = mix(grassC, grassC * vec3(1.35, 0.9, 0.45) + vec3(0.06, 0.03, 0.0), autumn * 0.8);
    vec3 forA = sAlb(2, uv * 0.35);
    vec3 leaves = sAlb(8, uv * 0.4);
    forA = mix(forA, leaves * vec3(1.1, 0.95, 0.8), autumn * 0.7);
    vec3 rockA = sAlb(3, uv * 0.25 + p.y * 0.06);
    vec3 mudA = sAlb(9, uv * 0.4);
    vec3 gravA = sAlb(7, uv * 0.8);

    vec3 base = mix(grassC, forA * 0.8, forestW);
    float springness = smoothstep(1.6, 0.4, abs(season - 1.0));
    base = mix(base, mudA * 0.8, clamp(springness * (1.0 - green) * 0.55 + wet * 0.25, 0.0, 0.7));
    base = mix(base, gravA, gravelW);
    base = mix(base, rockA, rockW);
    vec3 nrm = Ng;
    float nw = 0.0;
    vec3 tn = vec3(0, 0, 1);
    if (near > 0.01) {
        vec3 tg = sNrm(1, uv * 0.45);
        vec3 tf = sNrm(2, uv * 0.35);
        vec3 tr = sNrm(3, uv * 0.25 + p.y * 0.06);
        vec3 tgr = sNrm(7, uv * 0.8);
        tn = mix(tg, tf, forestW);
        tn = mix(tn, tgr, gravelW);
        tn = mix(tn, tr, rockW);
        nrm = perturb(Ng, p, uv, tn, 0.8 * near);
    }
    base *= macro;

    // far forest canopy tint so distant slopes read as spruce forest
    float farF = smoothstep(90.0, 260.0, dist) * smoothstep(0.25, 0.7, forest) * (1.0 - rockW) * (1.0 - smoothstep(130.0, 230.0, p.y));
    float cn = fbm3(p.xz * 0.18);
    vec3 canopy = mix(vec3(0.014, 0.034, 0.02), vec3(0.03, 0.06, 0.035), cn) * (0.7 + 0.6 * vnoise(p.xz * 0.9));
    canopy = mix(canopy, canopy * vec3(1.6, 1.0, 0.5), autumn * 0.35);
    base = mix(base, canopy, farF * 0.92);

    // snow
    float coverNoise = fbm3(p.xz * 0.12 + 3.1);
    float cov = snowCover * 1.35 - 0.25 * coverNoise - forestW * 0.12 * (1.0 - snowCover);
    cov += smoothstep(40.0, 160.0, p.y) * 0.25 * snowCover;
    cov -= wet * (1.0 - snowCover) * 0.5;
    float sm = clamp(cov * 2.2, 0.0, 1.0) * smoothstep(0.1, 0.45, Ng.y + (1.0 - rockW) * 0.15);
    sm *= 1.0 - gravelW * 0.4 * (1.0 - snowCover * 0.7);
    vec3 snowA = sAlb(0, uv * 0.3) * vec3(1.0, 1.02, 1.08);
    vec3 snowN = near > 0.01 ? sNrm(0, uv * 0.3) : vec3(0, 0, 1);
    base = mix(base, mix(snowA, vec3(0.8, 0.84, 0.9), 0.4) * 1.05, sm);
    nrm = normalize(mix(nrm, perturb(Ng, p, uv, snowN, 0.5 * near), sm * 0.7));
    // distant canopy still dusted white in winter
    base = mix(base, vec3(0.45, 0.5, 0.56), farF * snowCover * 0.55 * (1.0 - sm));
    s.alb = base;
    s.N = nrm;
    s.rough = mix(0.85, 0.7, sm);
    s.spec = mix(0.02, 0.25, sm) * (1.0 - rockW * 0.5);
    s.emis = 0.0;
    s.snow = sm;
    return s;
}

vec3 flagQC(vec2 uv) {
    vec3 blue = vec3(0.0, 0.08, 0.37), white = vec3(0.92, 0.92, 0.95);
    vec3 c = blue;
    if (abs(uv.x - 0.5) < 0.09 || abs(uv.y - 0.5) < 0.11) c = white;
    vec2 q = abs(uv - 0.5);
    vec2 f = abs(q - vec2(0.27, 0.25));
    float lis = step(length(f * vec2(1.4, 1.0)), 0.06);
    if (q.x > 0.09 && q.y > 0.11) c = mix(c, white, lis);
    return c;
}
vec3 flagCA(vec2 uv) {
    vec3 red = vec3(0.75, 0.04, 0.06), white = vec3(0.95, 0.95, 0.95);
    vec3 c = (uv.x < 0.25 || uv.x > 0.75) ? red : white;
    vec2 q = (uv - 0.5) * vec2(1.5, -1.0);
    float th = atan(q.y, q.x);
    float r = (1.0 + 0.9 * cos(8.0 * th)) * (1.0 + 0.1 * cos(24.0 * th)) * (0.9 + 0.05 * cos(200.0 * th)) * (1.0 + sin(th));
    if (length(q) < r * 0.115) c = red;
    return c;
}

void main() {
    int matId = int(vAttr.x + 0.5);
    vec3 N = normalize(vN);
    vec3 V = normalize(F.camPos.xyz - vPos);
    float dist = length(F.camPos.xyz - vPos);
    float ao = clamp(vAttr.y, 0.0, 1.0);
    Surf s;
    s.alb = vec3(0.5); s.N = N; s.rough = 0.7; s.spec = 0.03; s.emis = 0.0; s.snow = 0.0;
    float snowCover = F.w0.y;
    float alpha = 1.0;

    if (matId == 100) {
        s = terrainSurf(vPos, N, vAttr.y, vAttr.w, dist);
        ao = 1.0;
    } else if (matId <= 12) {
        if (matId == 12) {
            // deciduous leaf card (alpha tested, two sided)
            vec4 lc = sAlb4(12, vUV);
            float keep = pow(F.w0.w, 0.7);
            if (lc.a < 0.5 || vAttr.w > keep) discard;
            if (dot(N, V) < 0.0) N = -N;
            float autumn = F.w0.z;
            float v = vTint.w;
            vec3 summer = mix(vec3(0.10, 0.26, 0.05), vec3(0.18, 0.34, 0.07), v);
            vec3 spring = vec3(0.30, 0.46, 0.10);
            vec3 fall = mix(mix(vec3(0.75, 0.08, 0.02), vec3(0.88, 0.42, 0.03), v), vec3(0.85, 0.65, 0.08), smoothstep(0.7, 1.0, v));
            float springAmt = smoothstep(1.2, 0.0, abs(F.w0.x - 1.0)) * 0.7;
            vec3 col = mix(summer, spring, springAmt);
            col = mix(col, fall, smoothstep(0.0, 0.6, autumn));
            s.alb = col * lc.rgb * 1.6;
            s.N = N;
            s.rough = 0.7; s.spec = 0.04;
        } else {
            vec2 uv = vUV;
            vec3 a = sAlb(matId, uv);
            vec3 tn = sNrm(matId, uv);
            if (matId == 11) { tn = vec3(0, 0, 1); a *= 1.15; }
            if (matId == 10) a = mix(a, vec3(0.55, 0.7, 0.85), 0.35);
            s.alb = a * vTint.rgb;
            s.N = perturb(N, vPos, uv, tn, 0.9);
            s.rough = 0.8; s.spec = 0.03;
            if (matId == 10) { s.rough = 0.1; s.spec = 0.6; }
            if (matId == 4 || matId == 5 || matId == 6) { s.spec = 0.04; }
            if (matId == 3) { s.spec = 0.06; }
        }
        // snow accumulation on upward facing surfaces
        float aff = vAttr.w > 0.0 && matId != 12 ? vAttr.w : 0.0;
        float sm = snowMask(s.N, ao, matId == 11 ? 1.0 : 0.7, vPos, snowCover * (matId == 12 ? 0.0 : 1.0));
        vec3 snowA = sAlb(0, vPos.xz * 0.3 + vPos.y * 0.2) * vec3(1.0, 1.02, 1.08);
        s.alb = mix(s.alb, snowA, sm);
        s.spec = mix(s.spec, 0.25, sm);
        s.snow = sm;
    } else if (matId == 102) {
        // emissive windows / lights
        float lamp = F.w3.y;
        float flick = 0.94 + 0.06 * sin(F.camPos.w * 9.0 + vPos.x * 3.0 + vPos.z * 1.7);
        vec3 col0 = vec3(vUV, vAttr.w) * vTint.rgb;
        float isWin = vTint.w;   // 1 = window glass (dark by day), 0 = always lit light
        vec3 glass = F.skyHorizon.rgb * 0.35 + F.skyAmbient.rgb * 0.5;
        vec3 e = mix(col0 * (1.2 + 1.8 * lamp) * flick, mix(glass, col0 * 2.4 * flick, lamp), isWin);
        float dd = length(F.camPos.xyz - vPos);
        float ff = 1.0 - exp(-dd * F.fogColor.a - dd * F.w1.w * 0.012);
        e = mix(e, F.fogColor.rgb, clamp(ff, 0.0, 1.0));
        outColor = vec4(aces(e), 1.0);
        return;
    } else if (matId == 105) {
        s.alb = flagQC(vUV); s.rough = 0.8; s.spec = 0.02;
        if (dot(N, V) < 0.0) N = -N; s.N = N;
    } else if (matId == 106) {
        s.alb = flagCA(vUV); s.rough = 0.8; s.spec = 0.02;
        if (dot(N, V) < 0.0) N = -N; s.N = N;
    } else {
        // solid color materials (fur, fabric, metal, props)
        float n = vnoise(vPos.xz * 9.0 + vPos.y * 7.0);
        s.alb = vec3(vUV, vAttr.w) * vTint.rgb * (0.9 + 0.2 * n);
        s.rough = matId == 108 ? 0.4 : 0.8;
        s.spec = matId == 108 ? 0.35 : (matId == 101 ? 0.05 : 0.04);
        if (matId == 103) s.alb *= 0.9 + 0.2 * hash12(floor(vPos.xy * 60.0));
        float sm = snowMask(N, ao, 0.5, vPos, snowCover * vTint.w);
        s.alb = mix(s.alb, vec3(0.9, 0.93, 1.0), sm);
        s.snow = sm;
        s.N = N;
    }

    N = s.N;
    float rain = F.w1.y;
    float wetness = clamp(rain * 1.5, 0.0, 1.0) * (1.0 - s.snow);
    s.alb *= 1.0 - 0.35 * wetness;

    float shadow = shadowAt(vPos, normalize(vN));
    shadow = mix(1.0, shadow, F.sunColor.a);
    vec3 L = F.sunDir.xyz;
    float ndl = max(dot(N, L), 0.0);
    // wrap lighting for snow (subsurface-like softness)
    float wrap = mix(ndl, clamp((dot(N, L) + 0.35) / 1.35, 0.0, 1.0), s.snow * 0.8);
    vec3 diff = F.sunColor.rgb * wrap * shadow;
    float mdl = max(dot(N, F.moonDir.xyz), 0.0);
    diff += F.moonColor.rgb * mdl;
    vec3 amb = skyAmbientAt(N) * ao;
    // light bounce from snow ground
    amb += F.groundAmbient.rgb * 0.0;
    vec3 col = s.alb * (diff + amb);

    // dynamic point lights (cabin windows, lanterns, campfire)
    for (int i = 0; i < 12; i++) {
        vec4 lp = F.lightPos[i];
        if (lp.w <= 0.0) continue;
        vec3 d = lp.xyz - vPos;
        float r = length(d);
        if (r > lp.w) continue;
        float at = 1.0 - r / lp.w;
        at *= at;
        float nl = max(dot(N, d / r), 0.0) * 0.85 + 0.15 * at;
        col += s.alb * F.lightCol[i].rgb * F.lightCol[i].a * nl * at;
    }

    // specular
    vec3 H = normalize(L + V);
    float shin = mix(8.0, 90.0, 1.0 - s.rough);
    float sp = pow(max(dot(N, H), 0.0), shin) * s.spec * shadow * (1.0 + wetness * 3.0);
    col += F.sunColor.rgb * sp * (1.0 + shin * 0.02);
    col += F.moonColor.rgb * pow(max(dot(N, normalize(F.moonDir.xyz + V)), 0.0), shin) * s.spec * 0.5;
    // snow sparkle
    if (s.snow > 0.3 && F.sunDir.y > 0.0) {
        float g = hash12(floor(vPos.xz * 220.0) + floor(vPos.y * 50.0));
        float tw = step(0.9965, g) * (0.5 + 0.5 * sin(F.camPos.w * 2.0 + g * 80.0));
        col += F.sunColor.rgb * tw * shadow * 0.25 * s.snow;
    }
    // sky reflection fresnel (wet / ice)
    float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    vec3 R = reflect(-V, N);
    col += skyBase(vec3(R.x, abs(R.y), R.z)) * fres * (0.04 + 0.3 * wetness + s.spec * 0.3) ;

    // lightning
    col += vec3(0.5, 0.55, 0.8) * F.w2.y * s.alb * 1.5;

    // fog
    float fd = F.fogColor.a;
    float hfog = exp(-max(vPos.y - 5.0, 0.0) * 0.008);
    float fogF = 1.0 - exp(-dist * fd * (0.6 + 0.4 * hfog) - dist * F.w1.w * 0.012 * hfog);
    vec3 fc = F.fogColor.rgb;
    float sdv = max(dot(-V, F.sunDir.xyz), 0.0);
    fc += F.sunColor.rgb * pow(sdv, 6.0) * 0.06 * (1.0 - F.w1.x);
    col = mix(col, fc, clamp(fogF, 0.0, 1.0));

    outColor = vec4(aces(col), alpha);
}
