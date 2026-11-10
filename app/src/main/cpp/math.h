// Minimal math helpers (left-handed, x right / y up / z forward, column-major matrices).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

struct vec2 { float x = 0, y = 0; };
struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
struct vec4 { float x = 0, y = 0, z = 0, w = 0; };
inline vec3 operator*(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(vec3 a) { return std::sqrt(dot(a, a)); }
inline vec3 norm(vec3 a) { float l = len(a); return l > 1e-8f ? a * (1.0f / l) : vec3(0, 1, 0); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }
inline float fractf(float x) { return x - std::floor(x); }

struct mat4 {
    float m[16];  // column-major: m[col*4+row]
    static mat4 identity() { mat4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f; return r; }
};
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline mat4 translate(vec3 t) { mat4 r = mat4::identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline mat4 scale(vec3 s) { mat4 r = mat4::identity(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline mat4 rotY(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c; return r; }
inline mat4 rotX(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c; return r; }
inline mat4 rotZ(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[4] = -s; r.m[1] = s; r.m[5] = c; return r; }
inline vec3 xform(const mat4& m, vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
inline vec3 xformDir(const mat4& m, vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z, m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z, m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z};
}
// View matrix from basis (left-handed: forward is +z in view space)
inline mat4 viewFromBasis(vec3 eye, vec3 right, vec3 up, vec3 fwd) {
    mat4 r = mat4::identity();
    r.m[0] = right.x; r.m[4] = right.y; r.m[8] = right.z;
    r.m[1] = up.x;    r.m[5] = up.y;    r.m[9] = up.z;
    r.m[2] = fwd.x;   r.m[6] = fwd.y;   r.m[10] = fwd.z;
    r.m[12] = -dot(right, eye); r.m[13] = -dot(up, eye); r.m[14] = -dot(fwd, eye);
    return r;
}
// Reverse-Z infinite-ish perspective with Vulkan Y flip. depth: near->1, far->0.
inline mat4 perspectiveRevZ(float fovY, float aspect, float n, float f) {
    mat4 r; for (int i = 0; i < 16; i++) r.m[i] = 0;
    float t = 1.0f / std::tan(fovY * 0.5f);
    r.m[0] = t / aspect; r.m[5] = -t;
    r.m[10] = -n / (f - n); r.m[14] = n * f / (f - n); r.m[11] = 1.f;
    return r;
}
// Orthographic, depth 0..1 (normal Z), left-handed, with Y flip not needed for shadow
inline mat4 orthoLH(float l, float r_, float b, float t, float n, float f) {
    mat4 r = mat4::identity();
    r.m[0] = 2.f / (r_ - l); r.m[5] = 2.f / (t - b); r.m[10] = 1.f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -n / (f - n);
    return r;
}

// Hash / noise
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline float hash01(uint32_t x) { return (hash32(x) & 0xFFFFFF) / 16777216.0f; }
inline float hash2(int x, int y, uint32_t seed = 0) { return hash01((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ seed * 83492791u); }
inline float vnoise(float x, float y, uint32_t seed = 0) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}
inline float fbm(float x, float y, int oct, uint32_t seed = 0) {
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; i++) { s += a * vnoise(x, y, seed + i * 31); x = x * 2.03f + 17.1f; y = y * 2.03f + 9.7f; a *= 0.5f; }
    return s;
}
// Small deterministic RNG
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * next(); }
};
