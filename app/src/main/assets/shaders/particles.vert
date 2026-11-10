#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(push_constant) uniform PC { int type; int n; float a; float b; } pc;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

const vec2 Q[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));

void main() {
    int id = gl_InstanceIndex;
    vec2 c = Q[gl_VertexIndex % 6];
    vUV = c * 2.0;
    float t = F.camPos.w;
    vec3 cam = F.camPos.xyz;
    vec3 R = F.camRight.xyz, U = F.camUp.xyz;
    float fid = float(id);
    vec3 center = vec3(0.0);
    vec2 size = vec2(0.1);
    vec4 col = vec4(0.0);
    vec3 axisR = R, axisU = U;
    float day = clamp(F.sunDir.w * 3.0 + 0.3, 0.0, 1.0);
    vec3 light = F.skyAmbient.rgb * 2.2 + F.sunColor.rgb * 0.12 + F.moonColor.rgb * 0.3;
    float wind = F.w2.x;

    if (pc.type == 0) {            // snow
        float h1 = hash11(fid * 1.37), h2 = hash11(fid * 2.71 + 5.0), h3 = hash11(fid * 3.97 + 9.0);
        float Hh = 36.0;
        float sp = 0.9 + h3 * 0.9;
        float y = Hh - mod(t * sp + h1 * Hh, Hh);
        float depth = pow(h2, 1.6) * 110.0 + 1.5;
        float x = (hash11(fid * 5.1) - 0.5) * (30.0 + depth * 1.4);
        center = cam + F.camFwd.xyz * depth + R * x + vec3(0.0, y - 8.0, 0.0);
        center.x += sin(t * 0.7 + fid) * 0.6 + wind * (Hh - y) * 0.25;
        center.z += cos(t * 0.5 + fid * 1.3) * 0.4;
        float s = 0.03 + h3 * h3 * 0.05 + depth * 0.0018;
        size = vec2(s);
        col = vec4(light, 0.85 * smoothstep(0.0, 4.0, depth));
    } else if (pc.type == 1) {     // rain
        float h1 = hash11(fid * 1.37), h2 = hash11(fid * 2.71 + 5.0);
        float Hh = 30.0;
        float y = Hh - mod(t * 22.0 + h1 * Hh, Hh);
        float depth = pow(h2, 1.4) * 100.0 + 2.0;
        float x = (hash11(fid * 5.1) - 0.5) * (30.0 + depth * 1.4);
        center = cam + F.camFwd.xyz * depth + R * x + vec3(wind * (Hh - y) * 0.15, y - 8.0, 0.0);
        vec3 fall = normalize(vec3(wind * 0.25, -1.0, 0.0));
        vec3 side = normalize(cross(fall, F.camFwd.xyz));
        axisR = side; axisU = fall;
        size = vec2(0.012 + depth * 0.0004, 0.5 + depth * 0.004);
        col = vec4(light * 0.9 + vec3(0.1), 0.22);
    } else if (pc.type == 2) {     // chimney smoke
        int ch = id / 30;
        int pi = id % 30;
        vec4 cp = F.chimney[ch];
        if (cp.w < 0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vColor = vec4(0.0); return; }
        float ph = float(pi) / 30.0;
        float age = fract(t * 0.07 + ph + hash11(float(ch) * 7.3));
        float rise = age * 16.0;
        center = cp.xyz + vec3(sin(age * 6.0 + float(ch)) * 0.6 + wind * age * age * 14.0 + age * 1.5, rise, cos(age * 5.0 + float(ch)) * 0.4);
        float s = 0.45 + age * 3.4;
        size = vec2(s);
        float a = smoothstep(0.0, 0.07, age) * (1.0 - age) * 0.5;
        vec3 gcol = mix(vec3(0.38, 0.38, 0.42), vec3(0.85, 0.85, 0.88), day) * (0.25 + 0.75 * clamp(dot(light, vec3(0.33)) * 3.0, 0.0, 1.0));
        float night = F.w3.y;
        gcol += vec3(1.0, 0.45, 0.12) * 0.35 * night * (1.0 - age) * (1.0 - age);
        col = vec4(gcol, a);
    } else if (pc.type == 3) {     // falling leaves
        float h1 = hash11(fid * 1.37), h2 = hash11(fid * 2.71 + 5.0), h3 = hash11(fid * 3.97 + 9.0);
        float Hh = 28.0;
        float y = Hh - mod(t * (0.9 + h3 * 0.7) + h1 * Hh, Hh);
        float depth = pow(h2, 1.5) * 90.0 + 3.0;
        float x = (hash11(fid * 5.1) - 0.5) * (30.0 + depth * 1.4);
        center = cam + F.camFwd.xyz * depth + R * x + vec3(0.0, y - 8.0, 0.0);
        center.x += sin(t * 1.3 + fid * 2.1) * 1.2 + wind * (Hh - y) * 0.5;
        center.z += cos(t * 1.1 + fid) * 0.9;
        float ang = t * (1.0 + h3 * 3.0) + fid;
        mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
        vUV = rot * vUV;
        size = vec2(0.10 + h3 * 0.06) * vec2(1.0, 0.7);
        vec3 lc = h3 < 0.4 ? vec3(0.7, 0.1, 0.03) : (h3 < 0.75 ? vec3(0.85, 0.4, 0.04) : vec3(0.8, 0.6, 0.08));
        col = vec4(lc * (light * 1.2 + vec3(0.05)), 0.95);
    } else if (pc.type == 4) {     // embers (campfire) / fireflies
        vec4 cp = F.chimney[3];
        float h1 = hash11(fid * 1.37), h2 = hash11(fid * 2.71 + 5.0), h3 = hash11(fid * 3.97 + 9.0);
        if (pc.a < 0.5) {        // embers
            if (cp.w < 0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vColor = vec4(0.0); return; }
            float age = fract(t * (0.25 + h2 * 0.2) + h1);
            center = cp.xyz + vec3((h2 - 0.5) * 0.6 + sin(t * 2.0 + fid) * 0.3 * age + wind * age * 2.0, age * (2.0 + 3.0 * h3), (h3 - 0.5) * 0.5 + cos(t * 1.7 + fid) * 0.3 * age);
            size = vec2(0.05 * (1.0 - age));
            col = vec4(vec3(1.0, 0.45 + 0.3 * h1, 0.1) * 3.0, 1.0 - age);
        } else {                  // fireflies
            vec3 base = vec3(cam.x + (hash11(fid * 9.1) - 0.5) * 90.0, 0.0, cam.z + 8.0 + hash11(fid * 4.3) * 110.0);
            base.y = F.chimney[2].y * 0.0 + pc.b + (h2 * 3.0);
            center = base + vec3(sin(t * 0.6 + fid) * 2.0, sin(t * 0.9 + fid * 2.0) * 0.8, cos(t * 0.5 + fid * 1.7) * 2.0);
            size = vec2(0.09);
            float blink = pow(0.5 + 0.5 * sin(t * (1.0 + h3) + fid * 5.0), 3.0);
            col = vec4(vec3(0.7, 1.0, 0.25) * 2.0, blink * 0.9);
        }
    } else if (pc.type == 5) {     // fireworks
        int b = id / 70, s = id % 70;
        float hb = hash11(float(b) * 3.3);
        float period = 5.5;
        float tb = mod(t + float(b) * 1.9 + hb * 3.0, period);
        float burst = floor((t + float(b) * 1.9 + hb * 3.0) / period);
        vec3 site = vec3(cam.x + (hash11(float(b) * 7.7 + burst) - 0.5) * 220.0, 130.0 + hash11(float(b) * 1.9 + burst) * 90.0, cam.z + 330.0 + hash11(float(b) * 4.1 + burst) * 150.0);
        float launch = clamp(tb / 0.8, 0.0, 1.0);
        float te = max(tb - 0.8, 0.0);
        float hs = hash11(float(s) * 1.17 + float(b) * 13.0 + burst);
        float u = hash11(float(s) * 2.3 + float(b) * 5.0), v = hash11(float(s) * 3.1 + float(b) * 9.0 + burst);
        float th = u * 6.2831, z = v * 2.0 - 1.0, r = sqrt(1.0 - z * z);
        vec3 dir = vec3(r * cos(th), z, r * sin(th));
        float spd = 30.0 + hs * 6.0;
        center = site + dir * spd * (1.0 - exp(-te * 1.6)) * 0.9 + vec3(0.0, -6.0 * te * te * 0.5, 0.0);
        if (tb < 0.8) center = vec3(site.x, site.y - 120.0 * (1.0 - launch), site.z) + dir * 0.2;
        float fade = tb < 0.8 ? 0.8 : clamp(1.0 - te / 3.4, 0.0, 1.0);
        float hue = hash11(float(b) * 2.2 + burst);
        vec3 fc = hue < 0.33 ? vec3(1.0, 0.25, 0.2) : (hue < 0.66 ? vec3(0.3, 0.6, 1.0) : vec3(1.0, 0.8, 0.25));
        if (hs > 0.85) fc = vec3(1.0);
        size = vec2(1.6 + fade);
        col = vec4(fc * 3.0, fade * fade);
        float aOn = step(0.5, pc.a) ;
        col.a *= aOn;
    }
    vec3 wp = center + axisR * c.x * size.x + axisU * c.y * size.y;
    gl_Position = F.viewProj * vec4(wp, 1.0);
    // fog fade
    float dist = length(wp - cam);
    float fogF = 1.0 - exp(-dist * (F.fogColor.a + F.w1.w * 0.012));
    col.a *= 1.0 - clamp(fogF, 0.0, 0.9) * (pc.type == 5 ? 0.0 : 0.6);
    vColor = col;
}
