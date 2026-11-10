// Shared by all shaders: frame UBO, noise, sky, tonemap.
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 viewProj;
    mat4 lightVP;
    vec4 camPos;       // xyz, time
    vec4 sunDir;       // xyz, elevation
    vec4 sunColor;     // rgb, shadow strength
    vec4 moonDir;      // xyz, phase
    vec4 moonColor;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 skyAmbient;   // rgb, night
    vec4 groundAmbient;
    vec4 fogColor;     // rgb, density
    vec4 w0;           // season, snow, autumn, leaf
    vec4 w1;           // cloud, rain, snowfall, fog
    vec4 w2;           // wind, lightning, ice, aurora
    vec4 w3;           // events, -, flow, exposure
    vec4 camRight;     // xyz, tanHalfFovY
    vec4 camUp;        // xyz, aspect
    vec4 camFwd;
    vec4 lightPos[12];
    vec4 lightCol[12];
    vec4 chimney[4];
} F;

const float PI = 3.14159265;

float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
float hash13(vec3 p3) { p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}

vec3 aces(vec3 x) {
    x *= F.w3.w;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Base sky gradient + glows (no clouds / stars) used for reflections and fog too
vec3 skyBase(vec3 d) {
    float h = clamp(d.y, 0.0, 1.0);
    float t = pow(h, 0.42);
    vec3 c = mix(F.skyHorizon.rgb, F.skyZenith.rgb, t);
    float sd = max(dot(d, F.sunDir.xyz), 0.0);
    float glow = pow(sd, 5.0) * 0.05 + pow(sd, 40.0) * 0.18;
    c += F.sunColor.rgb * glow * (1.0 - 0.7 * F.w1.x);
    float md = max(dot(d, F.moonDir.xyz), 0.0);
    c += F.moonColor.rgb * pow(md, 30.0) * 0.5;
    return c;
}

vec3 skyAmbientAt(vec3 n) { return mix(F.groundAmbient.rgb, F.skyAmbient.rgb, n.y * 0.5 + 0.5); }
