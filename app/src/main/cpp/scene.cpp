// Procedural world: terrain, forest, cabins, stream, props; per-frame dynamic content (fauna, people, events).
#include <cstring>
#include <cmath>
#include "common.h"

namespace {

// ---------------- mesh building ----------------
struct MB {
    MeshData& m;
    explicit MB(MeshData& md) : m(md) {}
    uint32_t v(vec3 p, vec3 n, float u, float w, float mat, float ao = 1.f, float wind = 0.f, float aux = 0.f, vec3 col = vec3(1, 1, 1)) {
        Vertex x;
        x.pos[0] = p.x; x.pos[1] = p.y; x.pos[2] = p.z;
        x.nrm[0] = n.x; x.nrm[1] = n.y; x.nrm[2] = n.z;
        bool solid = mat >= 102.f && !(mat >= 104.5f && mat < 106.5f);
        x.uv[0] = solid ? col.x : u; x.uv[1] = solid ? col.y : w;
        x.attr[0] = mat; x.attr[1] = ao; x.attr[2] = wind; x.attr[3] = solid ? col.z : aux;
        m.v.push_back(x);
        return (uint32_t)m.v.size() - 1;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) { m.i.push_back(a); m.i.push_back(b); m.i.push_back(c); }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { tri(a, b, c); tri(a, c, d); }
};

void addBox(MB& mb, const mat4& xf, vec3 h, float mat, float uvScale, vec3 col = vec3(1, 1, 1), float ao = 1.f, float aux = 0.f) {
    struct Face { vec3 n, u, w; } faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
    for (auto& f : faces) {
        vec3 c = {f.n.x * h.x, f.n.y * h.y, f.n.z * h.z};
        vec3 eu = {f.u.x * h.x, f.u.y * h.y, f.u.z * h.z};
        vec3 ew = {f.w.x * h.x, f.w.y * h.y, f.w.z * h.z};
        float su = 2.f * len(eu) / uvScale, sw = 2.f * len(ew) / uvScale;
        vec3 n = norm(xformDir(xf, f.n));
        float aoF = f.n.y < -0.5f ? ao * 0.6f : ao;
        uint32_t a = mb.v(xform(xf, c - eu - ew), n, 0, 0, mat, aoF, 0, aux, col);
        uint32_t b = mb.v(xform(xf, c + eu - ew), n, su, 0, mat, aoF, 0, aux, col);
        uint32_t cc = mb.v(xform(xf, c + eu + ew), n, su, sw, mat, aoF, 0, aux, col);
        uint32_t d = mb.v(xform(xf, c - eu + ew), n, 0, sw, mat, aoF, 0, aux, col);
        mb.quad(a, b, cc, d);
    }
}

// Tapered cylinder along +y from 0..h
void addCyl(MB& mb, const mat4& xf, float r0, float r1, float h, int sides, float mat, float uvScale, bool caps = true, vec3 col = vec3(1, 1, 1),
            float ao = 1.f, float wind0 = 0.f, float wind1 = 0.f) {
    float slope = (r0 - r1) / std::max(h, 1e-4f);
    uint32_t base = (uint32_t)mb.m.v.size();
    for (int i = 0; i <= sides; i++) {
        float a = (float)i / sides * 6.2831853f;
        float cx = std::cos(a), sz = std::sin(a);
        vec3 n = norm(xformDir(xf, vec3(cx, slope, sz)));
        float u = (float)i / sides * 6.2831853f * std::max(r0, 0.05f) / uvScale;
        mb.v(xform(xf, vec3(cx * r0, 0, sz * r0)), n, u, 0, mat, ao, wind0, 0, col);
        mb.v(xform(xf, vec3(cx * r1, h, sz * r1)), n, u, h / uvScale, mat, ao, wind1, 0, col);
    }
    for (int i = 0; i < sides; i++) {
        uint32_t a = base + i * 2;
        mb.tri(a, a + 1, a + 3); mb.tri(a, a + 3, a + 2);
    }
    if (caps) {
        for (int c = 0; c < 2; c++) {
            float y = c ? h : 0, r = c ? r1 : r0;
            if (r <= 0.0001f) continue;
            vec3 n = norm(xformDir(xf, vec3(0, c ? 1.f : -1.f, 0)));
            uint32_t ctr = mb.v(xform(xf, vec3(0, y, 0)), n, 0.5f, 0.5f, mat, ao, c ? wind1 : wind0, 0, col);
            uint32_t first = (uint32_t)mb.m.v.size();
            for (int i = 0; i <= sides; i++) {
                float a = (float)i / sides * 6.2831853f;
                mb.v(xform(xf, vec3(std::cos(a) * r, y, std::sin(a) * r)), n, 0.5f + std::cos(a) * 0.5f, 0.5f + std::sin(a) * 0.5f, mat, ao, c ? wind1 : wind0, 0, col);
            }
            for (int i = 0; i < sides; i++) mb.tri(ctr, first + i, first + i + 1);
        }
    }
}

void addSphere(MB& mb, const mat4& xf, vec3 rad, int seg, int rings, float mat, vec3 col = vec3(1, 1, 1), float uvScale = 1.f, float ao = 1.f) {
    uint32_t base = (uint32_t)mb.m.v.size();
    for (int j = 0; j <= rings; j++) {
        float pol = (float)j / rings * 3.14159265f;
        for (int i = 0; i <= seg; i++) {
            float az = (float)i / seg * 6.2831853f;
            vec3 d = {std::sin(pol) * std::cos(az), std::cos(pol), std::sin(pol) * std::sin(az)};
            vec3 p = {d.x * rad.x, d.y * rad.y, d.z * rad.z};
            vec3 n = norm(vec3(d.x / rad.x, d.y / rad.y, d.z / rad.z));
            mb.v(xform(xf, p), norm(xformDir(xf, n)), (float)i / seg * 6.28f * rad.x / uvScale, (float)j / rings * 3.14f * rad.y / uvScale, mat, ao, 0, 0, col);
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            uint32_t a = base + j * (seg + 1) + i, b = a + 1, c = a + seg + 1, d = c + 1;
            mb.tri(a, c, b); mb.tri(b, c, d);
        }
}

// ---------------- trees ----------------
void buildFir(MeshData& md, int tiers, int sides, uint32_t seed) {
    MB mb(md);
    Rng rng(seed);
    // trunk (unit tree height = 1)
    addCyl(mb, translate({0, -0.02f, 0}), 0.028f, 0.006f, 1.0f, 6, M_BARK, 0.25f, true, vec3(1, 1, 1), 0.7f, 0, 1);
    for (int k = 0; k < tiers; k++) {
        float f = (float)k / (tiers - 1);
        float yBase = 0.09f + f * 0.74f;
        float tierH = 0.24f - 0.06f * f;
        float R = 0.30f * std::pow(1.f - f * 0.93f, 0.95f) + 0.015f;
        float droop = tierH * 0.55f;
        float aoT = 0.55f + 0.45f * f;
        float rot = rng.range(0, 6.28f);
        uint32_t apex = mb.v(vec3(0, yBase + tierH, 0), vec3(0, 1, 0), 0.5f, 0.0f, M_NEEDLE, aoT * 0.9f, 1.f, 0);
        // two rings: mid and outer
        std::vector<uint32_t> mid(sides + 1), outer(sides + 1), under(sides + 1);
        for (int i = 0; i <= sides; i++) {
            int ii = i % sides;
            float a = rot + (float)ii / sides * 6.2831853f;
            float jag = 0.78f + 0.4f * ((ii & 1) ? 1.f : 0.f) + 0.2f * hash2(ii, k, seed);
            float cx = std::cos(a), sz = std::sin(a);
            float Ro = R * jag;
            vec3 nMid = norm(vec3(cx * 0.55f, 0.85f, sz * 0.55f));
            vec3 nOut = norm(vec3(cx * 0.8f, 0.45f, sz * 0.8f));
            float u = (float)i / sides * 3.f;
            mid[i] = mb.v(vec3(cx * Ro * 0.55f, yBase + tierH * 0.58f, sz * Ro * 0.55f), nMid, u, 0.45f, M_NEEDLE, aoT * 0.8f, 1.f, 0);
            outer[i] = mb.v(vec3(cx * Ro, yBase + tierH * 0.1f - droop * 0.15f, sz * Ro), nOut, u, 1.0f, M_NEEDLE, aoT, 1.f, 0);
            vec3 nUnd = norm(vec3(cx * 0.3f, -1.f, sz * 0.3f));
            under[i] = mb.v(vec3(cx * Ro * 0.9f, yBase + tierH * 0.02f - droop * 0.2f, sz * Ro * 0.9f), nUnd, u, 1.2f, M_NEEDLE, 0.35f * aoT, 1.f, 0);
        }
        uint32_t ctrUnder = mb.v(vec3(0, yBase + tierH * 0.3f, 0), vec3(0, -1, 0), 0.5f, 0.5f, M_NEEDLE, 0.25f, 1.f, 0);
        for (int i = 0; i < sides; i++) {
            mb.tri(apex, mid[i], mid[i + 1]);
            mb.quad(mid[i], outer[i], outer[i + 1], mid[i + 1]);
            mb.tri(ctrUnder, under[i + 1], under[i]);
            mb.quad(outer[i], under[i], under[i + 1], outer[i + 1]);
        }
    }
    // normalise wind weight to height fraction (approx via y)
    for (auto& v : md.v) if (v.attr[2] > 0.f) v.attr[2] = clampf(v.pos[1], 0.1f, 1.f);
    // snow affinity for fir needles handled in shader (matId 11)
}

void branch(MB& mb, vec3 p, vec3 d, float L, float r, int depth, Rng& rng, float leafSize, bool leaves, int maxDepth) {
    vec3 up = std::fabs(d.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 side = norm(cross(up, d));
    vec3 fwd = cross(d, side);
    mat4 xf = mat4::identity();
    xf.m[0] = side.x; xf.m[1] = side.y; xf.m[2] = side.z;
    xf.m[4] = d.x; xf.m[5] = d.y; xf.m[6] = d.z;
    xf.m[8] = fwd.x; xf.m[9] = fwd.y; xf.m[10] = fwd.z;
    xf.m[12] = p.x; xf.m[13] = p.y; xf.m[14] = p.z;
    float r1 = r * 0.62f;
    addCyl(mb, xf, r, r1, L, depth == 0 ? 8 : 5, M_BARK, 0.3f, false, vec3(1, 1, 1), 0.8f, clampf(p.y, 0.f, 1.f), clampf(p.y + d.y * L, 0.f, 1.f));
    vec3 tip = p + d * L;
    if (depth >= maxDepth) {
        if (leaves) {
            for (int c = 0; c < 4; c++) {
                float a = rng.range(0, 6.28f);
                vec3 cdir = norm(vec3(std::cos(a), rng.range(-0.2f, 0.7f), std::sin(a)));
                vec3 center = tip + cdir * leafSize * 0.35f;
                float keep = rng.next();
                for (int q = 0; q < 2; q++) {
                    float ang = q * 1.5708f + rng.range(0, 3.14f);
                    vec3 ax = {std::cos(ang), 0, std::sin(ang)};
                    vec3 ay = norm(vec3(-ax.z * 0.3f, 1.f, ax.x * 0.3f));
                    float S = leafSize * rng.range(0.8f, 1.2f);
                    vec3 n = norm(cross(ax, ay));
                    float wind = clampf(center.y, 0.f, 1.f);
                    uint32_t a0 = mb.v(center - ax * S - ay * S, n, 0, 0, M_LEAFCARD, 0.9f, wind, keep);
                    uint32_t a1 = mb.v(center + ax * S - ay * S, n, 1, 0, M_LEAFCARD, 0.9f, wind, keep);
                    uint32_t a2 = mb.v(center + ax * S + ay * S, n, 1, 1, M_LEAFCARD, 1.0f, wind, keep);
                    uint32_t a3 = mb.v(center - ax * S + ay * S, n, 0, 1, M_LEAFCARD, 1.0f, wind, keep);
                    mb.quad(a0, a1, a2, a3);
                }
            }
        }
        return;
    }
    int kids = depth == 0 ? 4 : 3;
    for (int i = 0; i < kids; i++) {
        float a = rng.range(0, 6.28f);
        float tilt = rng.range(0.35f, 0.8f);
        vec3 nd = norm(d * std::cos(tilt) + (side * std::cos(a) + fwd * std::sin(a)) * std::sin(tilt));
        nd = norm(nd + vec3(0, 0.15f, 0));
        float t0 = depth == 0 ? rng.range(0.55f, 0.95f) : rng.range(0.6f, 1.0f);
        branch(mb, p + d * (L * t0), nd, L * rng.range(0.55f, 0.75f), r1 * 0.8f, depth + 1, rng, leafSize, leaves, maxDepth);
    }
    // continue trunk
    branch(mb, tip, norm(d + vec3(rng.range(-0.15f, 0.15f), 0, rng.range(-0.15f, 0.15f))), L * 0.7f, r1, depth + 1, rng, leafSize, leaves, maxDepth);
}

void buildDeciduous(MeshData& md, uint32_t seed, float spread, int maxDepth) {
    MB mb(md);
    Rng rng(seed);
    // trunk base 0.30 tall, then crown branches
    addCyl(mb, mat4::identity(), 0.03f, 0.022f, 0.30f, 8, M_BARK, 0.3f, false, vec3(1, 1, 1), 0.8f);
    branch(mb, vec3(0, 0.3f, 0), vec3(0, 1, 0), 0.38f, 0.022f, 0, rng, 0.17f * spread, true, maxDepth);
}

// ---------------- cabin ----------------
void buildCabin(MeshData& md, float W, float D, float wallH, bool shack, vec3* outChimney) {
    MB mb(md);
    float hw = W / 2, hd = D / 2;
    mat4 I = mat4::identity();
    // foundation (stone)
    addBox(mb, translate({0, 0.18f, 0}), {hw + 0.1f, 0.2f, hd + 0.1f}, M_ROCK, 1.6f, vec3(0.7f, 0.7f, 0.72f));
    // log walls built from stacked logs for the silhouette: horizontal cylinders
    int nLogs = (int)(wallH / 0.26f);
    for (int i = 0; i < nLogs; i++) {
        float y = 0.38f + i * 0.26f + 0.13f;
        float off = (i & 1) ? 0.09f : 0.0f;
        // front/back logs run along x, side logs along z (corner overhang)
        mat4 mx = translate({-hw - 0.22f - off, y, -hd}) * rotZ(-1.5708f);
        addCyl(mb, mx, 0.135f, 0.135f, W + 0.44f + 2 * off, 8, M_WOOD, 0.5f, true, vec3(1, 1, 1), 0.9f);
        mat4 mx2 = translate({-hw - 0.22f - off, y, hd}) * rotZ(-1.5708f);
        addCyl(mb, mx2, 0.135f, 0.135f, W + 0.44f + 2 * off, 8, M_WOOD, 0.5f, true, vec3(1, 1, 1), 0.9f);
        mat4 mz = translate({-hw, y + 0.0f, -hd - 0.22f - off}) * rotX(1.5708f);
        addCyl(mb, mz, 0.135f, 0.135f, D + 0.44f + 2 * off, 8, M_WOOD, 0.5f, true, vec3(1, 1, 1), 0.9f);
        mat4 mz2 = translate({hw, y + 0.0f, -hd - 0.22f - off}) * rotX(1.5708f);
        addCyl(mb, mz2, 0.135f, 0.135f, D + 0.44f + 2 * off, 8, M_WOOD, 0.5f, true, vec3(1, 1, 1), 0.9f);
    }
    // inner core box (hides gaps)
    addBox(mb, translate({0, 0.38f + wallH / 2, 0}), {hw - 0.05f, wallH / 2, hd - 0.05f}, M_WOOD, 1.4f, vec3(0.55f, 0.55f, 0.55f));
    // gable triangles (front/back) as boxes stepped
    float roofPitch = 0.62f;
    float ridgeH = hd * roofPitch;  // ridge runs along x
    float yEave = 0.38f + wallH;
    for (int side = 0; side < 2; side++) {
        float s = side ? 1.f : -1.f;
        // gable under roof on the short sides (x ends): triangle prism approximated by stacked boxes
        int steps = 5;
        for (int k = 0; k < steps; k++) {
            float f = (float)k / steps;
            float hz = hd * (1.f - f) * 0.98f;
            addBox(mb, translate({s * (hw - 0.04f), yEave + ridgeH * (f + 0.5f / steps), 0}), {0.12f, ridgeH / steps / 2 + 0.01f, hz}, M_WOOD, 1.2f, vec3(0.85f, 0.85f, 0.85f));
        }
    }
    // roof: two slabs sloping from ridge (z=0) down to eaves (z=±hd+overhang)
    float ov = 0.75f;
    float slabLen = std::sqrt(hd * hd + ridgeH * ridgeH) + ov;
    float ang = std::atan2(ridgeH, hd);
    for (int side = -1; side <= 1; side += 2) {
        float sx = (float)side;
        mat4 xf = translate({0, yEave + ridgeH + 0.05f, 0}) * rotX(-sx * ang) ;
        // slab centered along its length; local z axis = down the slope
        mat4 slab = translate({0, yEave + ridgeH * 0.5f + 0.08f, sx * hd * 0.5f}) * rotX(-sx * ang);
        addBox(mb, slab, {hw + ov, 0.09f, slabLen / 2}, M_ROOF, 1.8f, vec3(0.95f, 0.9f, 0.85f), 1.0f);
    }
    // ridge beam
    addCyl(mb, translate({-hw - ov, yEave + ridgeH + 0.12f, 0}) * rotZ(-1.5708f), 0.1f, 0.1f, W + 2 * ov, 6, M_WOOD, 0.5f);
    // chimney (stone) on the right of the roof
    float cx = hw * 0.55f, cz = hd * 0.0f + 0.1f;
    float chTop = yEave + ridgeH + 1.1f;
    addBox(mb, translate({cx, (chTop + 0.4f) / 2, cz}), {0.38f, (chTop - 0.4f) / 2, 0.38f}, M_ROCK, 0.9f, vec3(0.55f, 0.52f, 0.5f), 0.95f);
    addBox(mb, translate({cx, chTop + 0.08f, cz}), {0.46f, 0.08f, 0.46f}, M_ROCK, 0.9f, vec3(0.4f, 0.38f, 0.37f));
    if (outChimney) *outChimney = vec3(cx, chTop + 0.2f, cz);
    // front (-z) door and windows
    float fz = -hd - 0.25f;
    if (!shack) {
        addBox(mb, translate({-hw * 0.2f, 0.38f + 1.05f, fz - 0.02f}), {0.5f, 1.05f, 0.06f}, M_WOOD, 1.0f, vec3(0.45f, 0.3f, 0.2f));
        addBox(mb, translate({-hw * 0.2f, 0.38f + 2.18f, fz - 0.03f}), {0.62f, 0.08f, 0.08f}, M_WOOD, 1.0f, vec3(0.35f, 0.25f, 0.18f));
        // windows
        float wxs[2] = {hw * 0.45f, -hw * 0.72f};
        for (int w = 0; w < 2; w++) {
            float wx = wxs[w], wy = 0.38f + 1.45f;
            addBox(mb, translate({wx, wy, fz - 0.02f}), {0.62f, 0.5f, 0.05f}, M_WOOD, 1.0f, vec3(0.3f, 0.22f, 0.16f));
            MB& m2 = mb;
            // glass (emissive window)
            uint32_t g0 = m2.v(vec3(wx - 0.5f, wy - 0.4f, fz - 0.06f), vec3(0, 0, -1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.62f, 0.26f));
            uint32_t g1 = m2.v(vec3(wx + 0.5f, wy - 0.4f, fz - 0.06f), vec3(0, 0, -1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.62f, 0.26f));
            uint32_t g2 = m2.v(vec3(wx + 0.5f, wy + 0.4f, fz - 0.06f), vec3(0, 0, -1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.7f, 0.3f));
            uint32_t g3 = m2.v(vec3(wx - 0.5f, wy + 0.4f, fz - 0.06f), vec3(0, 0, -1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.7f, 0.3f));
            m2.quad(g0, g1, g2, g3);
            // mullions
            addBox(mb, translate({wx, wy, fz - 0.07f}), {0.03f, 0.42f, 0.03f}, M_WOOD, 1.0f, vec3(0.3f, 0.22f, 0.16f));
            addBox(mb, translate({wx, wy, fz - 0.07f}), {0.52f, 0.03f, 0.03f}, M_WOOD, 1.0f, vec3(0.3f, 0.22f, 0.16f));
        }
        // side windows (x = ±hw)
        for (int s = -1; s <= 1; s += 2) {
            float sx = s * (hw + 0.25f);
            float wy = 0.38f + 1.45f;
            addBox(mb, translate({sx + s * 0.02f, wy, 0}), {0.05f, 0.5f, 0.62f}, M_WOOD, 1.0f, vec3(0.3f, 0.22f, 0.16f));
            vec3 nn = {(float)s, 0, 0};
            uint32_t g0 = mb.v(vec3(sx + s * 0.06f, wy - 0.4f, -0.5f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.62f, 0.26f));
            uint32_t g1 = mb.v(vec3(sx + s * 0.06f, wy - 0.4f, 0.5f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.62f, 0.26f));
            uint32_t g2 = mb.v(vec3(sx + s * 0.06f, wy + 0.4f, 0.5f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.7f, 0.3f));
            uint32_t g3 = mb.v(vec3(sx + s * 0.06f, wy + 0.4f, -0.5f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.7f, 0.3f));
            mb.quad(g0, g1, g2, g3);
        }
        // porch
        addBox(mb, translate({-hw * 0.2f, 0.42f, fz - 0.9f}), {1.5f, 0.1f, 0.9f}, M_WOOD, 1.2f, vec3(0.8f, 0.7f, 0.6f));
        addBox(mb, translate({-hw * 0.2f, 0.25f, fz - 1.95f}), {1.0f, 0.07f, 0.28f}, M_WOOD, 1.2f, vec3(0.8f, 0.7f, 0.6f));
        for (int s = -1; s <= 1; s += 2)
            addCyl(mb, translate({-hw * 0.2f + s * 1.35f, 0.42f, fz - 1.65f}), 0.09f, 0.09f, 2.1f, 6, M_WOOD, 0.5f);
        // porch roof
        addBox(mb, translate({-hw * 0.2f, 2.55f, fz - 1.0f}) * rotX(0.12f), {1.7f, 0.06f, 1.1f}, M_ROOF, 1.8f, vec3(0.95f, 0.9f, 0.85f));
    } else {
        // sugar shack: wide door, vent cupola on ridge
        addBox(mb, translate({0, 0.38f + 1.0f, fz - 0.02f}), {0.9f, 1.0f, 0.06f}, M_WOOD, 1.0f, vec3(0.4f, 0.27f, 0.18f));
        for (int s = -1; s <= 1; s += 2) {
            float wy = 0.38f + 1.35f;
            vec3 nn = {0, 0, -1};
            float wx = s * hw * 0.62f;
            uint32_t g0 = mb.v(vec3(wx - 0.45f, wy - 0.35f, fz - 0.05f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.55f, 0.2f));
            uint32_t g1 = mb.v(vec3(wx + 0.45f, wy - 0.35f, fz - 0.05f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.55f, 0.2f));
            uint32_t g2 = mb.v(vec3(wx + 0.45f, wy + 0.35f, fz - 0.05f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.65f, 0.28f));
            uint32_t g3 = mb.v(vec3(wx - 0.45f, wy + 0.35f, fz - 0.05f), nn, 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.65f, 0.28f));
            mb.quad(g0, g1, g2, g3);
        }
        float cy = yEave + ridgeH + 0.1f;
        addBox(mb, translate({0, cy + 0.45f, 0}), {hw * 0.85f, 0.45f, 0.45f}, M_WOOD, 1.0f, vec3(0.7f, 0.6f, 0.5f));
        addBox(mb, translate({0, cy + 1.02f, 0}), {hw * 0.95f, 0.07f, 0.65f}, M_ROOF, 1.5f, vec3(0.95f, 0.9f, 0.85f));
        if (outChimney) *outChimney = vec3(0, cy + 1.2f, 0);
    }
}

void buildRock(MeshData& md, uint32_t seed) {
    MB mb(md);
    int seg = 14, rings = 9;
    uint32_t base = 0;
    std::vector<vec3> pts((seg + 1) * (rings + 1));
    for (int j = 0; j <= rings; j++)
        for (int i = 0; i <= seg; i++) {
            float pol = (float)j / rings * 3.14159f;
            float az = (float)(i % seg) / seg * 6.2831853f;
            vec3 d = {std::sin(pol) * std::cos(az), std::cos(pol), std::sin(pol) * std::sin(az)};
            float n = fbm(d.x * 1.8f + 5, d.z * 1.8f + d.y * 1.5f, 3, seed);
            float r = 0.65f + 0.7f * n;
            vec3 p = {d.x * r * 1.0f, d.y * r * 0.7f, d.z * r * 1.15f};
            if (p.y < -0.15f) p.y = -0.15f - (p.y + 0.15f) * 0.1f;
            pts[j * (seg + 1) + i] = p;
        }
    for (int j = 0; j <= rings; j++)
        for (int i = 0; i <= seg; i++) {
            vec3 p = pts[j * (seg + 1) + i];
            int ip = (i + 1) % (seg + 1), im = (i + seg) % (seg + 1);
            vec3 du = pts[j * (seg + 1) + ip] - pts[j * (seg + 1) + im];
            vec3 dv = pts[std::min(j + 1, rings) * (seg + 1) + i] - pts[std::max(j - 1, 0) * (seg + 1) + i];
            vec3 n = norm(cross(dv, du));
            if (dot(n, p) < 0) n = n * -1.f;
            mb.v(p, n, p.x * 0.9f + p.z * 0.3f, p.y * 0.9f + p.z * 0.6f, M_ROCK, 1.f, 0, 0);
        }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            uint32_t a = base + j * (seg + 1) + i, b = a + 1, c = a + seg + 1, d = c + 1;
            mb.tri(a, c, b); mb.tri(b, c, d);
        }
}

void buildBridge(MeshData& md) {
    MB mb(md);
    float L = 7.0f;
    // deck planks along x
    addBox(mb, translate({0, 0.0f, 0}), {L / 2, 0.07f, 0.85f}, M_WOOD, 1.2f, vec3(0.85f, 0.75f, 0.65f));
    for (int s = -1; s <= 1; s += 2) {
        addBox(mb, translate({0, -0.22f, s * 0.75f}), {L / 2, 0.14f, 0.1f}, M_WOOD, 1.0f, vec3(0.7f, 0.6f, 0.5f));
        // rail
        addBox(mb, translate({0, 0.95f, s * 0.82f}), {L / 2 - 0.2f, 0.05f, 0.05f}, M_WOOD, 1.0f, vec3(0.8f, 0.7f, 0.6f));
        addBox(mb, translate({0, 0.55f, s * 0.82f}), {L / 2 - 0.2f, 0.04f, 0.04f}, M_WOOD, 1.0f, vec3(0.8f, 0.7f, 0.6f));
        for (int k = 0; k <= 6; k++)
            addCyl(mb, translate({-L / 2 + 0.25f + k * (L - 0.5f) / 6, 0.05f, s * 0.82f}), 0.06f, 0.06f, 1.0f, 6, M_WOOD, 0.5f, true, vec3(0.8f, 0.7f, 0.6f));
    }
}

void buildLogs(MeshData& md) {
    MB mb(md);
    for (int layer = 0; layer < 4; layer++) {
        int n = 6 - layer;
        for (int i = 0; i < n; i++) {
            float x = (i - (n - 1) / 2.f) * 0.3f;
            addCyl(mb, translate({x, 0.15f + layer * 0.27f, -0.6f}) * rotX(1.5708f), 0.14f, 0.14f, 1.2f, 7, M_WOOD, 0.5f, true, vec3(0.9f, 0.85f, 0.8f), 0.9f);
        }
    }
}

void buildLamp(MeshData& md) {
    MB mb(md);
    addCyl(mb, mat4::identity(), 0.07f, 0.05f, 2.4f, 6, M_WOOD, 0.4f, true, vec3(0.6f, 0.5f, 0.4f));
    addBox(mb, translate({0, 2.5f, 0}), {0.17f, 0.2f, 0.17f}, M_EMISSIVE, 1.f, vec3(1.0f, 0.68f, 0.3f));
    addBox(mb, translate({0, 2.75f, 0}), {0.24f, 0.05f, 0.24f}, M_METAL, 1.f, vec3(0.08f, 0.08f, 0.09f));
}

void buildFlag(MeshData& md, bool pole) {
    MB mb(md);
    if (pole) {
        addCyl(mb, mat4::identity(), 0.07f, 0.04f, 7.0f, 6, M_METAL, 1.f, true, vec3(0.6f, 0.6f, 0.62f));
        addSphere(mb, translate({0, 7.05f, 0}), {0.08f, 0.08f, 0.08f}, 8, 6, M_METAL, vec3(0.9f, 0.75f, 0.2f));
    }
}

void buildFlagCloth(MeshData& md, float mat) {
    MB mb(md);
    // flag cloth, attached at x=0, extends along +x, hung at y=0..1.1, 12 segments for waving
    int seg = 14;
    float W = 1.7f, H = 1.13f;
    for (int i = 0; i <= seg; i++) {
        float u = (float)i / seg;
        mb.v(vec3(u * W, 0, 0), vec3(0, 0, 1), u, 1.0f, mat, 1, 0, 0);
        mb.v(vec3(u * W, H, 0), vec3(0, 0, 1), u, 0.0f, mat, 1, 0, 0);
    }
    for (int i = 0; i < seg; i++) {
        uint32_t a = i * 2;
        mb.tri(a, a + 2, a + 1); mb.tri(a + 1, a + 2, a + 3);
    }
    // uv.x already 0..1 along; flag waving in shader uses aUV.x
}

void buildHeart(MeshData& md) {
    MB mb(md);
    int n = 36;
    uint32_t c0 = mb.v(vec3(0, 0, 0.12f), vec3(0, 0, -1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.15f, 0.3f));
    uint32_t c1 = mb.v(vec3(0, 0, -0.12f), vec3(0, 0, 1), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.15f, 0.3f));
    std::vector<uint32_t> ring(n + 1);
    std::vector<uint32_t> ring2(n + 1);
    for (int i = 0; i <= n; i++) {
        float t = (float)(i % n) / n * 6.2831853f;
        float x = 16 * std::pow(std::sin(t), 3) / 17.f;
        float y = (13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t)) / 17.f;
        ring[i] = mb.v(vec3(x, y + 0.1f, 0.0f), vec3(x, y, 0).x != 0 ? norm(vec3(x, y, 0)) : vec3(0, 1, 0), 0, 0, M_EMISSIVE, 1, 0, 0, vec3(1.0f, 0.2f, 0.35f));
    }
    for (int i = 0; i < n; i++) { mb.tri(c0, ring[i], ring[i + 1]); mb.tri(c1, ring[i + 1], ring[i]); }
}

void buildSnowman(MeshData& md) {
    MB mb(md);
    vec3 white(0.95f, 0.97f, 1.0f);
    addSphere(mb, translate({0, 0.7f, 0}), {0.75f, 0.7f, 0.75f}, 18, 12, M_SOLID, white);
    addSphere(mb, translate({0, 1.8f, 0}), {0.55f, 0.52f, 0.55f}, 18, 12, M_SOLID, white);
    addSphere(mb, translate({0, 2.62f, 0}), {0.4f, 0.4f, 0.4f}, 16, 10, M_SOLID, white);
    // red sash (Bonhomme Carnaval): ring around the belly
    addCyl(mb, translate({0, 1.6f, 0}) * rotZ(0.0f), 0.57f, 0.55f, 0.2f, 16, M_SOLID, 1.f, false, vec3(0.8f, 0.05f, 0.08f));
    // toque (red cone cap)
    addCyl(mb, translate({0, 2.88f, 0}), 0.36f, 0.12f, 0.55f, 14, M_SOLID, 1.f, true, vec3(0.8f, 0.05f, 0.08f));
    addSphere(mb, translate({0, 3.45f, 0}), {0.1f, 0.1f, 0.1f}, 8, 6, M_SOLID, vec3(0.95f));
    // eyes + carrot nose
    for (int s = -1; s <= 1; s += 2) addSphere(mb, translate({s * 0.14f, 2.7f, -0.34f}), {0.045f, 0.045f, 0.045f}, 6, 4, M_SOLID, vec3(0.02f));
    addCyl(mb, translate({0, 2.62f, -0.36f}) * rotX(-1.5708f), 0.055f, 0.005f, 0.32f, 8, M_SOLID, 1.f, true, vec3(0.95f, 0.45f, 0.05f));
    // buttons
    for (int i = 0; i < 3; i++) addSphere(mb, translate({0, 1.35f + i * 0.28f, -0.5f}), {0.05f, 0.05f, 0.05f}, 6, 4, M_SOLID, vec3(0.02f));
}

void buildPrimitives(Scene& s) {
    { MB mb(s.meshes[MESH_SPHERE]); addSphere(mb, mat4::identity(), {1, 1, 1}, 16, 10, M_SOLID); }
    { MB mb(s.meshes[MESH_BOX]); addBox(mb, mat4::identity(), {0.5f, 0.5f, 0.5f}, M_SOLID, 1.f); }
    { MB mb(s.meshes[MESH_CYL]); addCyl(mb, mat4::identity(), 1, 1, 1, 10, M_SOLID, 1.f, true); }
    { MB mb(s.meshes[MESH_CONE]); addCyl(mb, mat4::identity(), 1, 0.0f, 1, 10, M_SOLID, 1.f, true); }
    // emissive variants (aliases: STUMP = sphere, BUSH = cone, GRASS_TUFT = box)
    { MB mb(s.meshes[MESH_STUMP]); addSphere(mb, mat4::identity(), {1, 1, 1}, 10, 7, M_EMISSIVE); }
    { MB mb(s.meshes[MESH_BUSH]); addCyl(mb, mat4::identity(), 1, 0.0f, 1, 8, M_EMISSIVE, 1.f, true); }
    { MB mb(s.meshes[MESH_GRASS_TUFT]); addBox(mb, mat4::identity(), {0.5f, 0.5f, 0.5f}, M_EMISSIVE, 1.f); }
}

Instance makeInst(const mat4& m, float r = 1, float g = 1, float b = 1, float a = 1) {
    Instance i;
    memcpy(i.model, m.m, 64);
    i.tint[0] = r; i.tint[1] = g; i.tint[2] = b; i.tint[3] = a;
    return i;
}

}  // namespace

// ---------------- terrain / world functions ----------------
float Scene::streamX(float z) const { return 7.f * std::sin(z * 0.028f + 0.5f) + 3.5f * std::sin(z * 0.071f + 1.3f) - 5.f; }

float Scene::baseHeight(float x, float z) const {
    float sx = streamX(z);
    float dx = x - sx;
    // valley floor rising slowly toward the mountains; flat across the stream
    float floorH = 0.045f * z - 3.f;
    float side = std::fabs(dx) - 12.f;
    float rise = side > 0 ? 0.0055f * side * side : 0.f;
    if (rise > 55.f) rise = 55.f + (rise - 55.f) * 0.5f;
    float hills = smoothstepf(20.f, 140.f, std::fabs(x)) * (14.f + 70.f * fbm(x * 0.012f + 3.1f, z * 0.012f, 4, 11));
    float rolling = (fbm(x * 0.03f, z * 0.03f, 4, 3) - 0.5f) * 5.f * smoothstepf(8.f, 40.f, std::fabs(dx));
    float far = smoothstepf(230.f, 1100.f, z);
    float mount = far * (420.f * (0.35f + 0.9f * fbm(x * 0.0032f + 7.f, z * 0.0032f, 5, 21)) + 60.f * fbm(x * 0.02f, z * 0.02f, 3, 8));
    float behind = smoothstepf(-60.f, -260.f, z) * 60.f;
    float sideFar = smoothstepf(150.f, 900.f, std::fabs(x)) * 260.f * (0.3f + fbm(x * 0.004f, z * 0.004f, 4, 33));
    return floorH + rise + hills + rolling + mount + behind + sideFar;
}

float Scene::rawHeight(float x, float z) const {
    float base = baseHeight(x, z);
    float sx = streamX(z);
    float d = std::fabs(x - sx);
    if (d > 6.f) return base;
    float ref = baseHeight(sx, z);
    float c = 1.f - smoothstepf(0.9f, 3.2f, d);
    float bed = ref - 1.2f;
    // banks slightly raised for a natural embankment
    float bank = smoothstepf(2.8f, 3.6f, d) * (1.f - smoothstepf(3.6f, 6.0f, d)) * 0.18f;
    return mixf(base, bed, c) + bank;
}

float Scene::rinkBlend(float x, float z) const {
    float dx = std::max(std::fabs(x - RINK_X) - 8.f, 0.f), dz = std::max(std::fabs(z - RINK_Z) - 5.f, 0.f);
    return 1.f - smoothstepf(0.f, 7.f, std::sqrt(dx * dx + dz * dz));
}

float Scene::heightAt(float x, float z) const {
    float h = rawHeight(x, z);
    float rb = rinkBlend(x, z);
    if (rb > 0.f) h = mixf(h, rinkH, rb);
    return h;
}
float Scene::waterLevel(float z) const { return baseHeight(streamX(z), z) - 0.55f; }

vec3 Scene::cameraPos(float scroll, float t) const {
    float x = mixf(-7.f, 7.f, scroll) + std::sin(t * 0.09f) * 0.25f;
    float z = -46.f;
    float y = heightAt(x, z) + 8.0f + std::sin(t * 0.07f) * 0.1f;
    return {x, y, z};
}

void Scene::build(int quality) {
    quality_ = quality;
    rinkH = rawHeight(RINK_X, RINK_Z);
    for (int i = 0; i < MESH_COUNT; i++) { shadowCaster[i] = false; shadowCount[i] = 0; }
    shadowCenter = {0, 6.f, 38.f};

    // cabins
    cabins = {{{-15.f, 0, 20.f}, 0.3f, 0}, {{17.f, 0, 42.f}, -0.4f, 0}, {{-30.f, 0, 72.f}, 0.55f, 1}};
    for (auto& c : cabins) c.pos.y = heightAt(c.pos.x, c.pos.z);
    campfire = {-5.f, 0, 8.f};
    campfire.y = heightAt(campfire.x, campfire.z);

    buildPrimitives(*this);
    buildFir(meshes[MESH_FIR_NEAR], 11, 14, 1);
    buildFir(meshes[MESH_FIR_MID], 8, 9, 2);
    buildFir(meshes[MESH_FIR_FAR], 5, 6, 3);
    buildFir(meshes[MESH_FIR_DECOR], 11, 14, 5);
    buildDeciduous(meshes[MESH_BIRCH], 41, 1.0f, 3);
    buildDeciduous(meshes[MESH_MAPLE], 77, 1.25f, 3);
    vec3 chim, chimS, chimShack;
    buildCabin(meshes[MESH_CABIN], 7.0f, 5.4f, 2.7f, false, &chim);
    buildCabin(meshes[MESH_CABIN_SMALL], 5.0f, 4.2f, 2.4f, false, &chimS);
    buildCabin(meshes[MESH_SHACK], 6.0f, 4.6f, 2.5f, true, &chimShack);
    buildRock(meshes[MESH_ROCK], 5);
    buildBridge(meshes[MESH_BRIDGE]);
    buildLogs(meshes[MESH_LOGS]);
    buildLamp(meshes[MESH_LAMP]);
    buildSnowman(meshes[MESH_SNOWMAN]);
    buildFlag(meshes[MESH_FLAGPOLE], true);
    buildFlagCloth(meshes[MESH_FLAG], M_FLAG_QC);
    buildFlagCloth(meshes[MESH_REED], M_FLAG_CA);
    buildHeart(meshes[MESH_HEART]);
    chimneyLocal[0] = chim; chimneyLocal[1] = chimS; chimneyLocal[2] = chimShack;

    // ---- terrain mesh (warped grid) ----
    {
        MB mb(meshes[MESH_TERRAIN]);
        const int N = 256;
        std::vector<float> xs(N + 1), zs(N + 1);
        for (int i = 0; i <= N; i++) {
            float t = (float)i / N * 2.f - 1.f;
            float at = std::fabs(t);
            xs[i] = (t < 0 ? -1.f : 1.f) * (150.f * at + 3000.f * at * at * at * at);
            float u = (float)i / N;
            zs[i] = -130.f + 330.f * u + 3200.f * u * u * u * u;
        }
        for (int j = 0; j <= N; j++)
            for (int i = 0; i <= N; i++) {
                float x = xs[i], z = zs[j];
                float h = heightAt(x, z);
                float dist = std::sqrt(x * x + (z + 46.f) * (z + 46.f));
                float e = std::max(0.6f, dist * 0.01f);
                vec3 n = norm(vec3(heightAt(x - e, z) - heightAt(x + e, z), 2.f * e, heightAt(x, z - e) - heightAt(x, z + e)));
                float forest = forestDensity(x, z);
                float wet = 1.f - smoothstepf(1.2f, 4.2f, std::fabs(x - streamX(z)));
                mb.v(vec3(x, h, z), n, x, z, M_TERRAIN, forest, 0, wet);
            }
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++) {
                uint32_t a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
                mb.tri(a, c, b); mb.tri(b, c, d);
            }
        statics[MESH_TERRAIN].push_back(makeInst(mat4::identity()));
        shadowCaster[MESH_TERRAIN] = true;
        shadowCount[MESH_TERRAIN] = 1;
    }

    // ---- water ribbon ----
    {
        MB mb(meshes[MESH_WATER]);
        int segs = 220;
        float z0 = -110.f, z1 = 340.f;
        for (int i = 0; i <= segs; i++) {
            float z = z0 + (z1 - z0) * i / segs;
            float sx = streamX(z);
            float wl = waterLevel(z);
            float hw = 2.35f;
            mb.v(vec3(sx - hw, wl, z), vec3(0, 1, 0), 0.f, z, M_WATER, 1, 0, 0);
            mb.v(vec3(sx + hw, wl, z), vec3(0, 1, 0), 1.f, z, M_WATER, 1, 0, 0);
        }
        for (int i = 0; i < segs; i++) { uint32_t a = i * 2; mb.tri(a, a + 1, a + 2); mb.tri(a + 1, a + 3, a + 2); }
        statics[MESH_WATER].push_back(makeInst(mat4::identity()));
    }

    // ---- cabins & props ----
    chimneys.clear();
    for (size_t ci = 0; ci < cabins.size(); ci++) {
        auto& c = cabins[ci];
        int mesh = c.type == 0 ? MESH_CABIN : MESH_CABIN_SMALL;
        mat4 m = translate(c.pos) * rotY(c.yaw);
        statics[mesh].push_back(makeInst(m));
        chimneys.push_back(xform(m, c.type == 0 ? chim : chimS));
        // woodpile beside the cabin
        vec3 wp = xform(m, vec3(c.type == 0 ? 4.3f : 3.3f, 0, -0.3f));
        statics[MESH_LOGS].push_back(makeInst(translate({wp.x, heightAt(wp.x, wp.z), wp.z}) * rotY(c.yaw + 1.57f)));
        // lantern post near the porch
        vec3 lp = xform(m, vec3(-2.6f, 0, -4.6f));
        statics[MESH_LAMP].push_back(makeInst(translate({lp.x, heightAt(lp.x, lp.z), lp.z})));
        lampPos.push_back({lp.x, heightAt(lp.x, lp.z) + 2.5f, lp.z});
    }
    // bridge
    {
        float bz = 14.f;
        float bx = streamX(bz);
        float by = baseHeight(bx, bz) - 0.05f;
        statics[MESH_BRIDGE].push_back(makeInst(translate({bx, by + 0.0f, bz})));
    }
    shadowCenter = {-2.f, 6.f, 30.f};

    // ---- vegetation scatter ----
    Rng rng(2024);
    int target[3] = {1900, 2800, 3900};
    int nTrees = target[quality < 0 ? 0 : (quality > 2 ? 2 : quality)];
    vec3 camP = {0, 0, -46.f};
    std::vector<Instance> firNear, firMid, firFar, firShNear, firShMid;
    int placed = 0, tries = 0;
    while (placed < nTrees && tries < nTrees * 30) {
        tries++;
        float x = rng.range(-150.f, 150.f);
        float z = rng.range(-70.f, 330.f);
        float fd = forestDensity(x, z);
        if (rng.next() > fd * fd) continue;
        // keep the spacing roughly even via jittered grid acceptance
        float y = heightAt(x, z);
        float slope = std::fabs(heightAt(x + 1.f, z) - y) + std::fabs(heightAt(x, z + 1.f) - y);
        if (slope > 1.6f) continue;
        if (y > 170.f) continue;
        float dist = std::sqrt((x - camP.x) * (x - camP.x) + (z - camP.z) * (z - camP.z));
        float sc = rng.range(0.7f, 1.35f) * (6.5f + 9.f * rng.next());
        if (dist < 30.f) continue;
        mat4 m = translate({x, y - 0.1f, z}) * rotY(rng.range(0, 6.28f)) * scale({sc * rng.range(0.85f, 1.15f), sc, sc * rng.range(0.85f, 1.15f)});
        float v = rng.range(0.85f, 1.15f);
        Instance in = makeInst(m, v * 0.95f, v, v * 0.95f, 1);
        bool inShadow = std::fabs(x - shadowCenter.x) < 100.f && std::fabs(z - shadowCenter.z) < 100.f;
        if (dist < 75.f) { (inShadow ? firShNear : firNear).push_back(in); }
        else if (dist < 170.f) { (inShadow ? firShMid : firMid).push_back(in); }
        else firFar.push_back(in);
        placed++;
    }
    auto merge = [&](int mesh, std::vector<Instance>& sh, std::vector<Instance>& rest) {
        statics[mesh] = sh;
        shadowCount[mesh] = (uint32_t)sh.size();
        statics[mesh].insert(statics[mesh].end(), rest.begin(), rest.end());
        shadowCaster[mesh] = true;
    };
    merge(MESH_FIR_NEAR, firShNear, firNear);
    merge(MESH_FIR_MID, firShMid, firMid);
    statics[MESH_FIR_FAR] = firFar;
    // deciduous trees (maple / birch) along banks and clearing edges
    {
        std::vector<Instance> mapleSh, birchSh;
        int cnt = 0;
        for (int tries2 = 0; tries2 < 4000 && cnt < 50; tries2++) {
            float z = rng.range(-10.f, 130.f);
            float x = streamX(z) + rng.range(-38.f, 38.f);
            if (std::fabs(x - streamX(z)) < 5.f) continue;
            bool nearCabin = false;
            for (auto& c : cabins) if (std::hypot(x - c.pos.x, z - c.pos.z) < 8.f) nearCabin = true;
            if (nearCabin) continue;
            float y = heightAt(x, z);
            float sc = rng.range(8.f, 14.f);
            mat4 m = translate({x, y - 0.1f, z}) * rotY(rng.range(0, 6.28f)) * scale({sc, sc, sc});
            float v = rng.next();
            Instance in = makeInst(m, rng.range(0.8f, 1.1f), rng.range(0.8f, 1.1f), rng.range(0.8f, 1.1f), v);
            ((cnt & 1) ? birchSh : mapleSh).push_back(in);
            cnt++;
        }
        statics[MESH_MAPLE] = mapleSh; statics[MESH_BIRCH] = birchSh;
        shadowCount[MESH_MAPLE] = (uint32_t)mapleSh.size(); shadowCount[MESH_BIRCH] = (uint32_t)birchSh.size();
        shadowCaster[MESH_MAPLE] = shadowCaster[MESH_BIRCH] = true;
    }
    // foreground shrubs: small spruces / young firs scattered near the camera
    for (int i = 0; i < 260; i++) {
        float z = rng.range(-42.f, 18.f);
        float x = rng.range(-45.f, 45.f);
        if (std::fabs(x - streamX(z)) < 4.f) continue;
        float y = heightAt(x, z);
        float sc = rng.range(1.2f, 3.6f);
        float v = rng.range(0.85f, 1.15f);
        statics[MESH_FIR_NEAR].push_back(makeInst(translate({x, y - 0.05f, z}) * rotY(rng.range(0, 6.28f)) * scale({sc * 0.9f, sc, sc * 0.9f}), v, v, v, 1));
    }
    // rocks along the stream and scattered
    for (int i = 0; i < 260; i++) {
        float z = i < 100 ? rng.range(-44.f, 20.f) : rng.range(-44.f, 260.f);
        float x = streamX(z) + (rng.next() < 0.6f ? rng.range(-2.6f, 2.6f) : rng.range(-30.f, 30.f));
        float y = heightAt(x, z);
        float s = rng.range(0.4f, 1.6f) * (rng.next() < 0.1f ? 2.0f : 1.f);
        if (z < 25.f) s = std::min(s, 1.0f);
        statics[MESH_ROCK].push_back(makeInst(translate({x, y - s * 0.15f, z}) * rotY(rng.range(0, 6.28f)) * scale({s, s, s}), 0.75f, 0.75f, 0.78f, 1));
    }
    shadowCaster[MESH_ROCK] = false;
    shadowCaster[MESH_CABIN] = shadowCaster[MESH_CABIN_SMALL] = shadowCaster[MESH_SHACK] = true;
    shadowCount[MESH_CABIN] = (uint32_t)statics[MESH_CABIN].size();
    shadowCount[MESH_CABIN_SMALL] = (uint32_t)statics[MESH_CABIN_SMALL].size();
    shadowCaster[MESH_BRIDGE] = true; shadowCount[MESH_BRIDGE] = (uint32_t)statics[MESH_BRIDGE].size();
    shadowCaster[MESH_LOGS] = true; shadowCount[MESH_LOGS] = (uint32_t)statics[MESH_LOGS].size();
    shadowCaster[MESH_LAMP] = true; shadowCount[MESH_LAMP] = (uint32_t)statics[MESH_LAMP].size();
    // dynamic-only shadow casters
    shadowCaster[MESH_SPHERE] = shadowCaster[MESH_BOX] = shadowCaster[MESH_CYL] = shadowCaster[MESH_CONE] = true;
    shadowCaster[MESH_SNOWMAN] = shadowCaster[MESH_FIR_DECOR] = shadowCaster[MESH_FLAGPOLE] = shadowCaster[MESH_SHACK] = true;
}

float Scene::forestDensity(float x, float z) const {
    float sx = streamX(z);
    float dxs = std::fabs(x - sx);
    float n = fbm(x * 0.011f + 40.f, z * 0.011f, 4, 61);
    float d = 0.25f + 0.85f * n;
    d *= 0.35f + 0.65f * smoothstepf(7.f, 48.f, dxs);
    d = mixf(d, 0.95f, smoothstepf(120.f, 260.f, z) * 0.7f);
    d = mixf(d, 1.f, smoothstepf(40.f, 120.f, std::fabs(x)) * 0.5f);
    // clearings around cabins, campfire and bridge
    for (auto& c : cabins) d *= smoothstepf(11.f, 24.f, std::hypot(x - c.pos.x, (z - c.pos.z) * 1.1f));
    d *= smoothstepf(6.f, 13.f, std::hypot(x - campfire.x, z - campfire.z));
    d *= smoothstepf(5.f, 12.f, std::hypot(x - streamX(14.f), z - 14.f));
    d *= mixf(0.15f, 1.f, smoothstepf(-20.f, 30.f, z));
    d *= 1.f - 0.9f * (1.f - smoothstepf(6.f, 16.f, dxs)) * (1.f - smoothstepf(10.f, 40.f, z));
    return clampf(d, 0.f, 1.f);
}

// ======================= dynamic content =======================
namespace {

float seasonWeight(float s, int k) {  // k: 0 winter, 1 spring, 2 summer, 3 autumn; s in [0,4)
    float d = std::fabs(s - k);
    d = std::min(d, 4.f - d);
    return clampf(1.f - d, 0.f, 1.f);
}

mat4 basisMat(vec3 a, vec3 b, float r) {
    vec3 d = b - a;
    float L = len(d);
    vec3 dn = L > 1e-5f ? d * (1.f / L) : vec3(0, 1, 0);
    vec3 up = std::fabs(dn.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 side = norm(cross(up, dn));
    vec3 fwd = cross(dn, side);
    mat4 m = mat4::identity();
    m.m[0] = side.x * r; m.m[1] = side.y * r; m.m[2] = side.z * r;
    m.m[4] = d.x; m.m[5] = d.y; m.m[6] = d.z;
    m.m[8] = fwd.x * r; m.m[9] = fwd.y * r; m.m[10] = fwd.z * r;
    m.m[12] = a.x; m.m[13] = a.y; m.m[14] = a.z;
    return m;
}

struct Dyn {
    std::vector<Instance>* d;
    void put(int mesh, const mat4& m, vec3 c, float a = 0.f) { d[mesh].push_back(makeInst(m, c.x, c.y, c.z, a)); }
    void ball(const mat4& base, vec3 p, vec3 r, vec3 c, float a = 0.f) { put(MESH_SPHERE, base * translate(p) * scale(r), c, a); }
    void ballRot(const mat4& base, vec3 p, vec3 r, float rx, vec3 c) { put(MESH_SPHERE, base * translate(p) * rotX(rx) * scale(r), c); }
    void limb(const mat4& base, vec3 a, vec3 b, float r, vec3 c) { put(MESH_CYL, base * basisMat(a, b, r), c); }
    void glow(vec3 p, float r, vec3 c, float win = 0.f) { put(MESH_STUMP, translate(p) * scale({r, r, r}), c, win); }
    void glowCone(vec3 p, float r, float h, vec3 c) { put(MESH_BUSH, translate(p) * scale({r, h, r}), c, 0.f); }
};

float pingPong(float t, float period, float pause, float& moving) {
    // 0..1 along A->B, pause at the ends; moving=1 when walking
    float cyc = std::fmod(t, period * 2.f);
    float seg = period;
    float f = cyc < seg ? cyc / seg : (2.f * seg - cyc) / seg;
    float k = clampf((f - pause) / (1.f - 2.f * pause), 0.f, 1.f);
    moving = (f > pause && f < 1.f - pause) ? 1.f : 0.f;
    k = k * k * (3 - 2 * k);
    return k;
}

}  // namespace

void Scene::update(const SceneParams& sp, std::vector<Instance> dyn[MESH_COUNT], FrameUBO& u) {
    const float* p = sp.p;
    float t = p[P_TIME];
    float hour = p[P_HOUR];
    float season = p[P_SEASON];
    int ev = sp.ev();
    float lamp = u.w3[1];
    float rain = p[P_RAIN], snowfall = p[P_SNOWFALL];
    float wx = p[P_WIND];
    Dyn D{dyn};
    float snowCover = p[P_SNOW];
    bool calm = rain < 0.6f && snowfall < 0.85f && p[P_FOG] < 0.8f;
    float wL = seasonWeight(season, 0), wS = seasonWeight(season, 1), wU = seasonWeight(season, 2), wA = seasonWeight(season, 3);
    bool dayTime = u.sunDir[3] > -0.05f;
    bool dawnDusk = (hour > 5.f && hour < 10.f) || (hour > 15.5f && hour < 21.5f);
    float night = u.skyAmbient[3];

    // ---------- lights & chimneys ----------
    int li = 0;
    auto addLight = [&](vec3 pos, float radius, vec3 col, float inten) {
        if (li >= 12) return;
        u.lightPos[li][0] = pos.x; u.lightPos[li][1] = pos.y; u.lightPos[li][2] = pos.z; u.lightPos[li][3] = radius;
        u.lightCol[li][0] = col.x; u.lightCol[li][1] = col.y; u.lightCol[li][2] = col.z; u.lightCol[li][3] = inten;
        li++;
    };
    float flick = 0.9f + 0.1f * std::sin(t * 7.1f) * std::sin(t * 3.3f + 1.f);
    for (size_t i = 0; i < cabins.size(); i++) {
        auto& c = cabins[i];
        mat4 m = translate(c.pos) * rotY(c.yaw);
        vec3 wpos = xform(m, vec3(c.type == 0 ? 1.4f : 0.8f, 1.9f, -4.4f));
        addLight(wpos, 10.f, {1.0f, 0.55f, 0.2f}, 1.7f * lamp * flick);
        if (i < lampPos.size()) addLight(lampPos[i], 9.f, {1.0f, 0.7f, 0.35f}, 1.5f * lamp);
        u.chimney[i < 2 ? i : 2][0] = chimneys[i].x; u.chimney[i < 2 ? i : 2][1] = chimneys[i].y; u.chimney[i < 2 ? i : 2][2] = chimneys[i].z;
        u.chimney[i < 2 ? i : 2][3] = (i < 2 || !(ev & EV_SUGAR)) ? 1.f : 0.f;
    }
    // chimney smoke slightly stronger when cold
    // campfire (evenings, or always during bonfire events)
    bool bonfire = (ev & (EV_QUEBEC | EV_CANADA)) != 0;
    float fireOn = (lamp > 0.25f || bonfire) && rain < 0.7f ? 1.f : 0.f;
    vec3 cf = campfire;
    if (bonfire) { cf = {streamX(26.f) + 6.f, 0, 26.f}; cf.y = heightAt(cf.x, cf.z); }
    u.chimney[3][0] = cf.x; u.chimney[3][1] = cf.y + 0.5f; u.chimney[3][2] = cf.z; u.chimney[3][3] = fireOn;
    if (fireOn > 0.f) {
        float fl = 0.8f + 0.2f * std::sin(t * 11.f) + 0.1f * std::sin(t * 17.f + 2.f);
        addLight(cf + vec3(0, 1.1f, 0), 15.f, {1.0f, 0.5f, 0.15f}, 2.6f * fl);
        float sz = bonfire ? 1.6f : 1.f;
        D.put(MESH_CYL, translate(cf + vec3(-0.4f * sz, 0.08f, 0)) * rotZ(1.2f) * scale({0.09f * sz, 0.9f * sz, 0.09f * sz}), {0.25f, 0.15f, 0.08f});
        D.put(MESH_CYL, translate(cf + vec3(0.4f * sz, 0.08f, 0.1f)) * rotZ(-1.2f) * scale({0.09f * sz, 0.9f * sz, 0.09f * sz}), {0.25f, 0.15f, 0.08f});
        D.put(MESH_CYL, translate(cf + vec3(0.1f, 0.08f, -0.4f * sz)) * rotX(-1.2f) * scale({0.09f * sz, 0.9f * sz, 0.09f * sz}), {0.25f, 0.15f, 0.08f});
        for (int i = 0; i < 8; i++) {
            float a = i / 8.f * 6.28f;
            D.ball(mat4::identity(), cf + vec3(std::cos(a) * 0.95f * sz, 0.1f, std::sin(a) * 0.95f * sz), {0.2f, 0.14f, 0.2f}, {0.45f, 0.45f, 0.47f});
        }
        for (int i = 0; i < 4; i++) {
            float ph = t * (5.f + i) + i * 2.f;
            float h = (0.55f + 0.25f * std::sin(ph) + 0.1f * std::sin(ph * 2.3f)) * sz;
            vec3 base = cf + vec3((i - 1.5f) * 0.12f * sz, 0.15f, std::sin(i * 1.7f) * 0.15f * sz);
            D.glowCone(base, (0.28f - i * 0.03f) * sz, h * (1.f + (i == 1) * 0.5f), vec3(1.0f, 0.38f + 0.1f * i, 0.05f));
        }
        D.glowCone(cf + vec3(0, 0.15f, 0), 0.18f * sz, 0.5f * sz, vec3(1.0f, 0.85f, 0.4f));
    }

    // ---------- fauna ----------
    auto quadruped = [&](vec3 pos, float yaw, float phase, float moving, float sc, vec3 body, vec3 belly, int antlers, bool moose, float headBob) {
        mat4 base = translate(pos) * rotY(yaw) * scale({sc, sc, sc});
        float sw = std::sin(phase) * 0.55f * moving;
        float bob = std::fabs(std::sin(phase)) * 0.03f * moving;
        mat4 B = base * translate({0, bob, 0});
        vec3 bz = {0, 0, 0};
        D.ball(B, {0, 1.12f, 0}, {0.3f, 0.32f, 0.72f}, body);
        D.ball(B, {0, 1.02f, 0.05f}, {0.27f, 0.25f, 0.6f}, belly);
        D.ball(B, {0, 1.18f, 0.55f}, {0.25f, 0.3f, 0.3f}, body);             // chest
        D.ball(B, {0, 1.1f, -0.62f}, {0.28f, 0.3f, 0.32f}, body);            // rump
        if (moose) D.ball(B, {0, 1.38f, 0.38f}, {0.24f, 0.2f, 0.3f}, body);  // shoulder hump
        // neck + head
        float hy = moose ? 1.52f : 1.72f;
        vec3 neckA = {0, 1.28f, 0.62f}, neckB = {0, hy, 0.98f + headBob * 0.2f};
        D.limb(B, neckA, neckB, moose ? 0.17f : 0.11f, body);
        vec3 headC = neckB + vec3(0, moose ? -0.14f : -0.06f, 0.2f);
        D.ballRot(B, headC, {moose ? 0.16f : 0.1f, moose ? 0.2f : 0.11f, moose ? 0.36f : 0.22f}, 0.5f, body);
        D.ball(B, headC + vec3(0, -0.06f, moose ? 0.32f : 0.2f), {moose ? 0.1f : 0.055f, moose ? 0.1f : 0.055f, 0.1f}, moose ? vec3(0.12f, 0.08f, 0.06f) : vec3(0.1f, 0.08f, 0.07f));
        for (int s = -1; s <= 1; s += 2) D.ballRot(B, neckB + vec3(s * 0.12f, 0.1f, 0.04f), {0.04f, 0.1f, 0.05f}, 0.f, body);  // ears
        // antlers
        if (antlers) {
            vec3 ac = vec3(0.55f, 0.45f, 0.33f);
            for (int s = -1; s <= 1; s += 2) {
                vec3 a0 = neckB + vec3(s * 0.06f, 0.12f, 0.05f);
                if (moose) {
                    vec3 a1 = a0 + vec3(s * 0.45f, 0.1f, 0.05f);
                    D.limb(B, a0, a1, 0.025f, ac);
                    D.ball(B, a1 + vec3(s * 0.12f, 0.05f, 0.1f), {0.28f, 0.03f, 0.25f}, ac);
                } else {
                    vec3 a1 = a0 + vec3(s * 0.12f, 0.35f, -0.08f);
                    D.limb(B, a0, a1, 0.014f, ac);
                    D.limb(B, a1 - vec3(0, 0.12f, 0), a1 + vec3(s * 0.08f, 0.15f, 0.12f), 0.01f, ac);
                    D.limb(B, a1, a1 + vec3(s * 0.1f, 0.2f, -0.12f), 0.01f, ac);
                }
            }
        }
        // legs
        for (int l = 0; l < 4; l++) {
            float lx = (l & 1) ? 0.17f : -0.17f;
            float lz = (l & 2) ? -0.52f : 0.5f;
            float th = sw * ((l == 0 || l == 3) ? 1.f : -1.f);
            vec3 H = {lx, 1.0f, lz};
            vec3 K = H + vec3(0, -std::cos(th) * 0.5f, std::sin(th) * 0.5f);
            float th2 = th * 0.4f - 0.25f * std::max(0.f, std::sin(phase + (l == 0 || l == 3 ? 0.f : 3.14f))) * moving;
            vec3 F2 = K + vec3(0, -std::cos(th2) * 0.5f, std::sin(th2) * 0.5f);
            D.limb(B, H, K, 0.065f, body);
            D.limb(B, K, F2, 0.04f, moose ? vec3(0.1f, 0.08f, 0.06f) : vec3(0.2f, 0.17f, 0.13f));
        }
        // tail
        D.ball(B, {0, 1.28f, -0.92f}, {0.06f, 0.1f, 0.06f}, moose ? vec3(0.1f, 0.07f, 0.05f) : vec3(0.95f, 0.95f, 0.92f));
    };

    if (calm) {
        // deer: dawn/dusk, midday in winter
        bool deerOn = (dawnDusk || (wL > 0.5f && hour > 9.f && hour < 16.f)) && dayTime;
        if (deerOn) {
            vec3 A = {-12.f, 0, 54.f}, Bp = {-6.f, 0, 27.f};
            float mv; float k = pingPong(t, 70.f, 0.12f, mv);
            float x = mixf(A.x, Bp.x, k), z = mixf(A.z, Bp.z, k);
            float cyc = std::fmod(t, 140.f);
            float dx = (cyc < 70.f ? Bp.x - A.x : A.x - Bp.x), dz = (cyc < 70.f ? Bp.z - A.z : A.z - Bp.z);
            float yaw = std::atan2(dx, dz);
            vec3 body = wL > 0.5f ? vec3(0.32f, 0.26f, 0.21f) : vec3(0.42f, 0.27f, 0.15f);
            // grazing head dip when stopped
            quadruped({x, heightAt(x, z), z}, yaw, t * 4.5f, mv, 1.0f, body, {0.75f, 0.7f, 0.62f}, (wA + wL > 0.4f) ? 1 : 0, false, 0);
            // doe following
            float k2 = pingPong(t - 4.f, 70.f, 0.12f, mv);
            float x2 = mixf(A.x - 3.f, Bp.x - 4.f, k2), z2 = mixf(A.z + 2.f, Bp.z + 3.f, k2);
            quadruped({x2, heightAt(x2, z2), z2}, yaw, t * 4.5f + 1.f, mv, 0.82f, body, {0.75f, 0.7f, 0.62f}, 0, false, 0);
        }
        // moose in the stream
        bool mooseOn = (wU + wS + wA > 0.3f) && ((hour > 5.f && hour < 10.f) || (hour > 17.f && hour < 21.f)) && dayTime;
        if (mooseOn) {
            float mv; float k = pingPong(t + 20.f, 90.f, 0.2f, mv);
            float z = mixf(80.f, 58.f, k);
            float x = streamX(z) + 0.4f;
            float yaw = std::fmod(t + 20.f, 180.f) < 90.f ? std::atan2(streamX(58.f) - streamX(80.f), -22.f) : std::atan2(streamX(80.f) - streamX(58.f), 22.f);
            quadruped({x, waterLevel(z) - 0.45f, z}, yaw, t * 3.2f, mv, 1.35f, {0.14f, 0.09f, 0.06f}, {0.2f, 0.14f, 0.1f}, (wU + wS > 0.4f) ? 0 : 1, true, 0);
        }
        // fox at dusk/night in cold seasons
        if ((wL + wA + wS) > 0.5f && ((hour > 16.f && hour < 23.f) || (hour > 3.f && hour < 8.f))) {
            float mv; float k = pingPong(t + 7.f, 55.f, 0.1f, mv);
            float x = mixf(14.f, 30.f, k), z = mixf(30.f, 48.f, k);
            float yaw = std::fmod(t + 7.f, 110.f) < 55.f ? std::atan2(16.f, 18.f) : std::atan2(-16.f, -18.f);
            mat4 base = translate({x, heightAt(x, z), z}) * rotY(yaw);
            vec3 fur = {0.75f, 0.32f, 0.07f};
            float ph = t * 8.f, sw2 = std::sin(ph) * 0.6f * mv;
            D.ball(base, {0, 0.38f, 0}, {0.12f, 0.13f, 0.32f}, fur);
            D.ball(base, {0, 0.4f, 0.33f}, {0.09f, 0.09f, 0.12f}, fur);
            D.ball(base, {0, 0.4f, 0.47f}, {0.045f, 0.045f, 0.07f}, {0.05f, 0.04f, 0.04f});
            for (int s = -1; s <= 1; s += 2) D.limb(base, {s * 0.05f, 0.47f, 0.36f}, {s * 0.06f, 0.6f, 0.34f}, 0.025f, fur);
            D.limb(base, {0, 0.38f, -0.3f}, {0, 0.34f, -0.8f}, 0.07f, fur);
            D.ball(base, {0, 0.34f, -0.85f}, {0.06f, 0.06f, 0.09f}, {0.95f, 0.95f, 0.95f});
            for (int l = 0; l < 4; l++) {
                float lx = (l & 1) ? 0.07f : -0.07f, lz = (l & 2) ? -0.22f : 0.2f;
                float th = sw2 * ((l == 0 || l == 3) ? 1.f : -1.f);
                D.limb(base, {lx, 0.3f, lz}, {lx, 0.0f, lz + std::sin(th) * 0.18f}, 0.03f, {0.12f, 0.08f, 0.05f});
            }
        }
        // loon on the stream (spring / summer / autumn dawn & dusk)
        if ((wS + wU + wA) > 0.3f && dawnDusk && dayTime && p[P_ICE] < 0.4f) {
            float z = 44.f + std::sin(t * 0.04f) * 5.f;
            float x = streamX(z) + std::sin(t * 0.3f) * 0.5f;
            float yaw = std::atan2(streamX(z) - streamX(z - 1.f), 1.f) + 3.14f;
            mat4 b = translate({x, waterLevel(z) + 0.02f, z}) * rotY(yaw + 0.0f);
            D.ball(b, {0, 0.12f, 0}, {0.17f, 0.12f, 0.4f}, {0.05f, 0.06f, 0.07f});
            D.ball(b, {0, 0.1f, 0.15f}, {0.15f, 0.08f, 0.2f}, {0.95f, 0.95f, 0.95f});
            D.limb(b, {0, 0.18f, 0.35f}, {0, 0.5f, 0.42f}, 0.05f, {0.04f, 0.05f, 0.06f});
            D.ball(b, {0, 0.52f, 0.46f}, {0.06f, 0.06f, 0.09f}, {0.03f, 0.04f, 0.06f});
            D.limb(b, {0, 0.51f, 0.53f}, {0, 0.5f, 0.66f}, 0.018f, {0.5f, 0.5f, 0.5f});
        }
        // geese in V formation (spring & autumn, daytime)
        if ((wS + wA) > 0.4f && dayTime && hour > 7.f && hour < 19.f) {
            float cyc = std::fmod(t, 150.f) / 150.f;
            if (cyc < 0.5f) {
                float u2 = cyc / 0.5f;
                float dir = wS > wA ? 1.f : -1.f;  // north in spring, south in autumn (visually: left/right)
                float cx = mixf(-190.f, 190.f, dir > 0 ? u2 : 1.f - u2);
                for (int i = 0; i < 9; i++) {
                    int rk = (i + 1) / 2, sd = (i & 1) ? 1 : -1;
                    if (i == 0) { rk = 0; sd = 0; }
                    vec3 bp = {cx - dir * rk * 3.2f * 1.2f, 66.f + std::sin(t * 0.3f + i) * 0.8f + rk * 0.4f, 150.f + sd * rk * 3.0f * 1.3f};
                    float flap = std::sin(t * 7.f + i * 0.7f);
                    mat4 b = translate(bp) * rotY(dir > 0 ? 1.5708f : -1.5708f);
                    vec3 gc = {0.14f, 0.12f, 0.1f};
                    D.ball(b, {0, 0, 0}, {0.35f, 0.35f, 1.1f}, gc);
                    D.limb(b, {0, 0.1f, 0.9f}, {0, 0.45f, 1.5f}, 0.1f, {0.06f, 0.06f, 0.07f});
                    D.ball(b, {0, 0.5f, 1.55f}, {0.14f, 0.12f, 0.2f}, {0.05f, 0.05f, 0.06f});
                    for (int s = -1; s <= 1; s += 2) {
                        vec3 sh = {s * 0.25f, 0.1f, 0.2f};
                        vec3 tipw = sh + vec3(s * 1.7f, flap * 0.9f, -0.35f);
                        D.limb(b, sh, tipw, 0.2f, gc);
                    }
                }
            }
        }
        // fisherman: seated at the bank (warm seasons) or ice fishing (winter), daytime
        if (dayTime && hour > 7.f && hour < 19.f && snowfall < 0.5f) {
            float z = 27.f;
            float bx = streamX(z) - 3.4f;
            vec3 pos = {bx, heightAt(bx, z), z};
            mat4 b = translate(pos) * rotY(1.5708f);
            vec3 jacket = wL > 0.5f ? vec3(0.7f, 0.12f, 0.1f) : vec3(0.15f, 0.3f, 0.5f);
            // seated body
            D.limb(b, {0, 0.45f, -0.1f}, {0, 0.95f, 0.0f}, 0.2f, jacket);
            D.ball(b, {0, 1.12f, 0.02f}, {0.12f, 0.13f, 0.12f}, {0.85f, 0.68f, 0.55f});
            D.ball(b, {0, 1.2f, 0.0f}, {0.15f, 0.07f, 0.15f}, wL > 0.5f ? vec3(0.1f, 0.1f, 0.12f) : vec3(0.4f, 0.32f, 0.2f));
            D.limb(b, {0, 0.45f, -0.1f}, {0, 0.45f, 0.5f}, 0.09f, {0.15f, 0.18f, 0.28f});
            D.limb(b, {0, 0.45f, 0.5f}, {0, 0.05f, 0.55f}, 0.08f, {0.15f, 0.18f, 0.28f});
            // rod
            float lift = std::max(0.f, std::sin(t * 0.25f)) * 0.3f;
            vec3 hand = {0.2f, 0.85f, 0.35f};
            vec3 rodTip = {1.4f, 1.55f + lift, 1.3f};
            D.limb(b, {0, 0.9f, 0.1f}, hand, 0.06f, jacket);
            D.limb(b, hand - vec3(0.1f, 0.05f, -0.1f), rodTip, 0.012f, {0.2f, 0.15f, 0.1f});
            D.limb(b, rodTip, {1.7f, 0.1f, 2.0f}, 0.003f, {0.9f, 0.9f, 0.9f});
            D.ball(b, {1.7f, 0.1f + std::sin(t * 2.f) * 0.02f, 2.0f}, {0.04f, 0.04f, 0.04f}, {0.9f, 0.1f, 0.1f});
            // stool
            D.put(MESH_BOX, b * translate({0, 0.2f, -0.05f}) * scale({0.35f, 0.4f, 0.35f}), {0.3f, 0.2f, 0.12f});
        }
        // skaters on the rink (winter) and skier
        if (snowCover > 0.35f && (dayTime || lamp > 0.3f)) {
            for (int i = 0; i < 3; i++) {
                float a = t * (0.35f + i * 0.07f) + i * 2.1f;
                float rx = 5.2f * std::cos(a), rz = 3.0f * std::sin(a);
                float dx = -5.2f * std::sin(a), dz = 3.0f * std::cos(a);
                vec3 pos = {RINK_X + rx * (0.8f + 0.1f * i), rinkH + 0.05f, RINK_Z + rz * (0.8f + 0.1f * i)};
                float yaw = std::atan2(dx, dz);
                mat4 b = translate(pos) * rotY(yaw);
                vec3 cols[3] = {{0.8f, 0.1f, 0.1f}, {0.1f, 0.35f, 0.75f}, {0.9f, 0.7f, 0.1f}};
                float glide = std::sin(t * 2.2f + i) * 0.3f;
                D.limb(b, {0, 0.85f, 0}, {0, 1.4f, 0.12f}, 0.17f, cols[i]);
                D.ball(b, {0, 1.55f, 0.15f}, {0.11f, 0.12f, 0.11f}, {0.85f, 0.68f, 0.55f});
                D.ball(b, {0, 1.63f, 0.15f}, {0.13f, 0.09f, 0.13f}, cols[(i + 1) % 3]);
                D.limb(b, {0.08f, 0.85f, 0}, {0.1f, 0.05f, glide * 0.4f}, 0.065f, {0.12f, 0.12f, 0.18f});
                D.limb(b, {-0.08f, 0.85f, 0}, {-0.1f, 0.05f, -glide * 0.6f - 0.3f}, 0.065f, {0.12f, 0.12f, 0.18f});
                D.limb(b, {0.17f, 1.3f, 0.1f}, {0.45f, 1.0f, 0.2f}, 0.05f, cols[i]);
                D.limb(b, {-0.17f, 1.3f, 0.1f}, {-0.4f, 1.05f, -0.1f}, 0.05f, cols[i]);
            }
        }
        if (snowCover > 0.5f && dayTime && hour > 9.f && hour < 16.f) {
            float mv; float k = pingPong(t, 60.f, 0.08f, mv);
            float z = mixf(112.f, 70.f, k), x = mixf(56.f, 44.f, k);
            float yaw = std::fmod(t, 120.f) < 60.f ? std::atan2(-12.f, -42.f) : std::atan2(12.f, 42.f);
            mat4 b = translate({x, heightAt(x, z) + 0.05f, z}) * rotY(yaw) * rotX(0.f);
            vec3 jc = {0.9f, 0.4f, 0.05f};
            D.limb(b, {0, 0.8f, 0}, {0, 1.4f, 0.2f}, 0.17f, jc);
            D.ball(b, {0, 1.55f, 0.25f}, {0.11f, 0.12f, 0.11f}, {0.1f, 0.1f, 0.12f});
            for (int s = -1; s <= 1; s += 2) {
                D.put(MESH_BOX, b * translate({s * 0.12f, 0.03f, 0.1f}) * scale({0.07f, 0.03f, 1.8f}), {0.1f, 0.1f, 0.12f});
                D.limb(b, {s * 0.09f, 0.8f, 0}, {s * 0.12f, 0.1f, -0.1f}, 0.065f, {0.1f, 0.12f, 0.2f});
                D.limb(b, {s * 0.17f, 1.3f, 0.2f}, {s * 0.4f, 0.8f, 0.7f}, 0.04f, jc);
                D.limb(b, {s * 0.4f, 0.8f, 0.7f}, {s * 0.45f, 0.02f, 0.45f}, 0.012f, {0.7f, 0.7f, 0.7f});
            }
        }
    }

    // rink geometry (winter)
    if (snowCover > 0.35f || p[P_TEMP] < -2.f) {
        D.put(MESH_BOX, translate({RINK_X, rinkH + 0.02f, RINK_Z}) * scale({15.f, 0.06f, 9.4f}), {0.8f, 0.95f, 1.1f}, 0.f);
        for (int s = -1; s <= 1; s += 2) {
            D.put(MESH_BOX, translate({RINK_X, rinkH + 0.3f, RINK_Z + s * 4.8f}) * scale({15.6f, 0.6f, 0.15f}), {0.85f, 0.85f, 0.88f}, 1.f);
            D.put(MESH_BOX, translate({RINK_X + s * 7.8f, rinkH + 0.3f, RINK_Z}) * scale({0.15f, 0.6f, 9.6f}), {0.85f, 0.85f, 0.88f}, 1.f);
        }
        D.put(MESH_LAMP, translate({RINK_X - 8.4f, heightAt(RINK_X - 8.4f, RINK_Z - 5.4f), RINK_Z - 5.4f}), {1, 1, 1}, 0.f);
        D.put(MESH_LAMP, translate({RINK_X + 8.4f, heightAt(RINK_X + 8.4f, RINK_Z - 5.4f), RINK_Z - 5.4f}), {1, 1, 1}, 0.f);
        addLight({RINK_X - 8.4f, rinkH + 2.6f, RINK_Z - 5.4f}, 14.f, {0.9f, 0.85f, 0.7f}, 1.4f * lamp);
        addLight({RINK_X + 8.4f, rinkH + 2.6f, RINK_Z - 5.4f}, 14.f, {0.9f, 0.85f, 0.7f}, 1.4f * lamp);
    }

    // ---------- events ----------
    // eave string lights
    auto eaveLights = [&](vec3 c1, vec3 c2, vec3 c3, bool twinkle, float spacing) {
        for (auto& c : cabins) {
            float W = c.type == 0 ? 7.0f : 5.0f, Dp = c.type == 0 ? 5.4f : 4.2f, wallH = c.type == 0 ? 2.7f : 2.4f;
            float hd = Dp / 2, hw = W / 2;
            float ridgeH = hd * 0.62f, ov = 0.75f;
            float slabLen = std::sqrt(hd * hd + ridgeH * ridgeH) + ov, ang = std::atan2(ridgeH, hd);
            float yE = 0.38f + wallH;
            float ez = -(hd * 0.5f + slabLen * 0.5f * std::cos(ang));
            float ey = yE + ridgeH * 0.5f + 0.08f - slabLen * 0.5f * std::sin(ang) - 0.04f;
            mat4 m = translate(c.pos) * rotY(c.yaw);
            int n = (int)((W + 2 * ov) / spacing);
            for (int i = 0; i <= n; i++) {
                float x = -hw - ov + i * (W + 2 * ov) / n;
                float sag = std::sin((float)i / n * 3.14159f) * 0.0f;
                vec3 col = (i % 3 == 0) ? c1 : ((i % 3 == 1) ? c2 : c3);
                float tw = twinkle ? 0.55f + 0.45f * std::sin(t * 3.f + i * 1.7f) : 1.f;
                vec3 pw = xform(m, vec3(x, ey + sag, ez - 0.05f));
                D.glow(pw, 0.075f, col * tw);
            }
        }
    };
    if (ev & EV_CHRISTMAS) {
        eaveLights({1.0f, 0.1f, 0.08f}, {0.1f, 1.0f, 0.2f}, {1.0f, 0.75f, 0.2f}, true, 0.5f);
        // decorated tree
        vec3 tp = {-9.5f, 0, 31.f}; tp.y = heightAt(tp.x, tp.z);
        float sc = 8.5f;
        D.put(MESH_FIR_DECOR, translate(tp) * scale({sc, sc, sc}), {1, 1, 1}, 1.f);
        for (int i = 0; i < 46; i++) {
            float f = (float)i / 46.f;
            float y = (0.12f + 0.7f * f) * sc;
            float R = (0.30f * std::pow(1.f - f * 0.93f, 0.95f) * 0.72f + 0.02f) * sc * 1.0f;
            float a = i * 2.4f;
            vec3 cols[4] = {{1.f, 0.1f, 0.1f}, {1.f, 0.8f, 0.2f}, {0.2f, 0.5f, 1.f}, {0.2f, 1.f, 0.4f}};
            float tw = 0.6f + 0.4f * std::sin(t * 2.5f + i);
            D.glow(tp + vec3(std::cos(a) * R, y, std::sin(a) * R - 0.3f), 0.1f, cols[i % 4] * tw);
        }
        D.glowCone(tp + vec3(0, sc * 0.98f, 0), 0.2f, 0.5f, {1.f, 0.85f, 0.3f});
        D.glow(tp + vec3(0, sc * 0.98f + 0.4f, 0), 0.22f, {1.f, 0.9f, 0.4f});
        addLight(tp + vec3(0, 3.5f, -2.5f), 14.f, {1.0f, 0.75f, 0.45f}, 1.6f * lamp * (0.9f + 0.1f * std::sin(t * 2.f)));
        // snowman buddy
        vec3 sp = {-3.5f, 0, 36.f}; sp.y = heightAt(sp.x, sp.z);
        D.put(MESH_SNOWMAN, translate(sp) * rotY(2.9f) * scale({0.9f, 0.9f, 0.9f}), {1, 1, 1}, 1.f);
        // wreaths on doors
        for (auto& c : cabins) {
            float hw = (c.type == 0 ? 3.5f : 2.5f);
            mat4 m = translate(c.pos) * rotY(c.yaw);
            for (int i = 0; i < 12; i++) {
                float a = i / 12.f * 6.28f;
                vec3 pw = xform(m, vec3(-hw * 0.2f + std::cos(a) * 0.32f, 2.4f + std::sin(a) * 0.32f, -(c.type == 0 ? 2.7f : 2.1f) - 0.38f));
                D.ball(mat4::identity(), pw, {0.09f, 0.09f, 0.09f}, {0.05f, 0.3f, 0.1f});
            }
            vec3 bw = xform(m, vec3(-hw * 0.2f, 2.08f, -(c.type == 0 ? 2.7f : 2.1f) - 0.4f));
            D.ball(mat4::identity(), bw, {0.14f, 0.1f, 0.06f}, {0.9f, 0.05f, 0.05f});
        }
    }
    if (ev & EV_NEWYEAR) eaveLights({1.0f, 0.85f, 0.5f}, {1.0f, 0.95f, 0.8f}, {0.9f, 0.9f, 1.0f}, true, 0.45f);
    if (ev & EV_VALENTINE) {
        eaveLights({1.0f, 0.15f, 0.3f}, {1.0f, 0.5f, 0.65f}, {0.9f, 0.1f, 0.5f}, true, 0.6f);
        for (size_t ci = 0; ci < cabins.size(); ci++) {
            auto& c = cabins[ci];
            mat4 m = translate(c.pos) * rotY(c.yaw);
            for (int i = 0; i < 5; i++) {
                float hx = -2.4f + i * 1.2f;
                vec3 pw = xform(m, vec3(hx, (c.type == 0 ? 2.7f : 2.4f) + 0.3f, -(c.type == 0 ? 3.5f : 3.0f) - 0.2f));
                D.put(MESH_HEART, translate(pw) * rotY(c.yaw) * scale({0.3f, 0.3f, 0.3f}), {1.0f, 1.0f, 1.0f});
            }
        }
        // floating hearts rising from the first chimney
        for (int i = 0; i < 12; i++) {
            float age = std::fmod(t * 0.08f + i / 12.f, 1.f);
            vec3 ch = chimneys[0];
            vec3 pw = ch + vec3(std::sin(age * 9.f + i) * 1.2f + wx * age * 8.f, 1.5f + age * 22.f, std::cos(age * 7.f + i) * 0.8f);
            float s2 = 0.4f + 0.3f * (1.f - age);
            float fade = clampf(std::min(age * 8.f, (1.f - age) * 4.f), 0.f, 1.f);
            D.put(MESH_HEART, translate(pw) * rotY(t * 0.8f + i) * scale({s2, s2, s2}), {fade, fade, fade});
        }
    }
    if (ev & EV_STPATRICK) {
        eaveLights({0.1f, 1.0f, 0.25f}, {0.2f, 0.9f, 0.4f}, {1.0f, 0.85f, 0.2f}, true, 0.5f);
        vec3 pg = {streamX(22.f) + 4.5f, 0, 22.f}; pg.y = heightAt(pg.x, pg.z);
        D.ball(mat4::identity(), pg + vec3(0, 0.35f, 0), {0.45f, 0.38f, 0.45f}, {0.04f, 0.04f, 0.05f});
        for (int i = 0; i < 14; i++) D.glow(pg + vec3(std::cos(i * 1.3f) * 0.3f, 0.6f + (i % 3) * 0.04f, std::sin(i * 1.3f) * 0.3f), 0.07f, vec3(1.0f, 0.8f, 0.15f) * (0.7f + 0.3f * std::sin(t * 4.f + i)));
        addLight(pg + vec3(0, 1.f, 0), 8.f, {1.0f, 0.85f, 0.3f}, 1.3f);
    }
    if (ev & EV_HALLOWEEN) {
        eaveLights({1.0f, 0.4f, 0.02f}, {0.55f, 0.1f, 0.85f}, {1.0f, 0.5f, 0.05f}, true, 0.6f);
        Rng pr(99);
        for (size_t ci = 0; ci < cabins.size(); ci++) {
            auto& c = cabins[ci];
            mat4 m = translate(c.pos) * rotY(c.yaw);
            for (int i = 0; i < 4; i++) {
                vec3 pw = xform(m, vec3(-1.6f + i * 0.9f + pr.range(-0.2f, 0.2f), 0, -(c.type == 0 ? 4.6f : 4.0f) - pr.range(0.f, 1.4f)));
                pw.y = heightAt(pw.x, pw.z);
                float sc = 0.3f + 0.15f * pr.next();
                D.put(MESH_SPHERE, translate(pw + vec3(0, sc * 0.8f, 0)) * scale({sc * 1.2f, sc, sc * 1.2f}), {0.95f, 0.42f, 0.04f}, 0.2f);
                if (i % 2 == 0) {  // jack-o-lantern face glow
                    vec3 fp = pw + vec3(0, sc * 0.85f, 0) + xformDir(rotY(c.yaw), vec3(0, 0, -sc * 1.15f));
                    float fl = 0.8f + 0.2f * std::sin(t * 9.f + i * 3.f);
                    D.glow(fp + xformDir(rotY(c.yaw), vec3(-sc * 0.35f, sc * 0.25f, 0)), sc * 0.14f, vec3(1.0f, 0.6f, 0.1f) * fl);
                    D.glow(fp + xformDir(rotY(c.yaw), vec3(sc * 0.35f, sc * 0.25f, 0)), sc * 0.14f, vec3(1.0f, 0.6f, 0.1f) * fl);
                    D.glow(fp + xformDir(rotY(c.yaw), vec3(0, -sc * 0.25f, 0)), sc * 0.2f, vec3(1.0f, 0.5f, 0.08f) * fl);
                    addLight(fp + vec3(0, 0.2f, -0.2f), 5.f, {1.0f, 0.5f, 0.1f}, 1.2f * fl * lamp);
                }
            }
        }
        // bats at dusk
        if (night > 0.2f || lamp > 0.4f)
            for (int i = 0; i < 6; i++) {
                float a = t * (0.5f + 0.1f * i) + i * 1.7f;
                vec3 bp = {-10.f + std::cos(a) * 14.f + i * 2.f, 14.f + std::sin(a * 1.7f) * 3.f + i, 40.f + std::sin(a) * 12.f};
                float fl = std::sin(t * 18.f + i);
                mat4 b = translate(bp) * rotY(a + 1.57f);
                D.ball(b, {0, 0, 0}, {0.12f, 0.08f, 0.2f}, {0.02f, 0.02f, 0.03f});
                for (int s = -1; s <= 1; s += 2) D.limb(b, {s * 0.08f, 0, 0}, {s * 0.6f, fl * 0.3f, -0.1f}, 0.07f, {0.02f, 0.02f, 0.03f});
            }
    }
    if (ev & EV_THANKS) {
        Rng pr(55);
        for (size_t ci = 0; ci < cabins.size(); ci++) {
            auto& c = cabins[ci];
            mat4 m = translate(c.pos) * rotY(c.yaw);
            for (int i = 0; i < 3; i++) {
                vec3 pw = xform(m, vec3(3.8f + i * 1.2f, 0, -3.0f - pr.range(0.f, 1.5f)));
                pw.y = heightAt(pw.x, pw.z);
                D.put(MESH_CYL, translate(pw + vec3(0, 0.0f, 0)) * rotY(c.yaw) * rotZ(1.5708f) * translate({0, -0.6f, 0}) * scale({0.5f, 1.2f, 0.5f}), {0.8f, 0.65f, 0.3f}, 0.8f);
            }
            for (int i = 0; i < 5; i++) {
                vec3 pw = xform(m, vec3(-3.4f + i * 0.5f + pr.range(-0.2f, 0.2f), 0, -3.4f - pr.range(0.2f, 1.2f)));
                pw.y = heightAt(pw.x, pw.z);
                float sc = 0.25f + 0.12f * pr.next();
                D.put(MESH_SPHERE, translate(pw + vec3(0, sc * 0.8f, 0)) * scale({sc * 1.2f, sc, sc * 1.2f}), {0.92f, 0.45f, 0.06f}, 0.2f);
            }
            for (int i = 0; i < 4; i++) {  // corn stalks
                vec3 pw = xform(m, vec3(-4.2f + i * 0.25f, 0, -2.0f));
                pw.y = heightAt(pw.x, pw.z);
                D.put(MESH_BUSH, translate(pw) * scale({0.35f, 2.0f, 0.35f}), {0.75f, 0.65f, 0.25f}, 0.f);
            }
        }
    }
    if (ev & EV_EASTER) {
        Rng pr(7);
        vec3 cols[6] = {{1.f, 0.3f, 0.4f}, {0.3f, 0.6f, 1.f}, {1.f, 0.85f, 0.2f}, {0.5f, 0.9f, 0.4f}, {0.8f, 0.4f, 0.9f}, {1.f, 0.6f, 0.2f}};
        for (int i = 0; i < 22; i++) {
            float x = -26.f + pr.range(0.f, 30.f), z = 18.f + pr.range(0.f, 36.f);
            if (std::fabs(x - streamX(z)) < 4.f) continue;
            float y = heightAt(x, z);
            D.put(MESH_SPHERE, translate({x, y + 0.1f, z}) * rotY(pr.range(0, 6.f)) * rotZ(pr.range(-0.5f, 0.5f)) * scale({0.09f, 0.13f, 0.09f}), cols[i % 6], 0.f);
        }
        // rabbit hopping
        float hop = std::fabs(std::sin(t * 3.f));
        float rcyc = std::fmod(t, 40.f) / 40.f;
        float rx = mixf(-16.f, -4.f, rcyc), rz = 40.f + std::sin(rcyc * 6.28f) * 4.f;
        mat4 b = translate({rx, heightAt(rx, rz) + hop * 0.25f, rz}) * rotY(1.2f);
        vec3 fur = {0.8f, 0.72f, 0.62f};
        D.ball(b, {0, 0.22f, 0}, {0.16f, 0.17f, 0.25f}, fur);
        D.ball(b, {0, 0.34f, 0.28f}, {0.1f, 0.1f, 0.12f}, fur);
        for (int s = -1; s <= 1; s += 2) D.limb(b, {s * 0.04f, 0.4f, 0.3f}, {s * 0.05f, 0.75f, 0.24f}, 0.025f, fur);
        D.ball(b, {0, 0.25f, -0.27f}, {0.06f, 0.06f, 0.06f}, {1.f, 1.f, 1.f});
    }
    if (ev & (EV_QUEBEC | EV_CANADA)) {
        for (size_t ci = 0; ci < cabins.size(); ci++) {
            auto& c = cabins[ci];
            mat4 m = translate(c.pos) * rotY(c.yaw);
            vec3 pw = xform(m, vec3(c.type == 0 ? 4.6f : 3.6f, 0, -4.5f));
            pw.y = heightAt(pw.x, pw.z);
            D.put(MESH_FLAGPOLE, translate(pw), {1, 1, 1}, 0.f);
            bool qc = (ev & EV_QUEBEC) && (!(ev & EV_CANADA) || ci % 2 == 0);
            D.put(qc ? MESH_FLAG : MESH_REED, translate(pw + vec3(0.04f, 5.7f, 0)) * rotY(0.0f), {1, 1, 1}, 0.f);
        }
        // pole near the bonfire too
    }
    if (ev & EV_CARNAVAL) {
        vec3 sp = {-14.5f, 0, 24.f}; sp.y = heightAt(sp.x, sp.z);
        D.put(MESH_SNOWMAN, translate(sp) * rotY(0.4f) * scale({1.5f, 1.5f, 1.5f}), {1, 1, 1}, 1.f);
        // ice palace
        vec3 ip = {RINK_X - 1.f, 0, RINK_Z + 11.f}; ip.y = heightAt(ip.x, ip.z);
        float ph = t * 0.4f;
        for (int i = 0; i < 5; i++) {
            float h = 3.f + 1.2f * (i % 3);
            float ox = (i - 2) * 2.2f;
            D.put(MESH_BOX, translate(ip + vec3(ox, h * 0.5f, 0)) * scale({1.6f, h, 1.6f}), {0.75f, 0.9f, 1.1f}, 0.f);
            D.put(MESH_BUSH, translate(ip + vec3(ox, h, 0)) * scale({1.1f, 1.6f, 1.1f}), {0.7f, 0.9f, 1.1f}, 0.f);
        }
        vec3 lc = {0.5f + 0.5f * std::sin(ph), 0.5f + 0.5f * std::sin(ph + 2.1f), 0.5f + 0.5f * std::sin(ph + 4.2f)};
        addLight(ip + vec3(0, 2.5f, -3.f), 16.f, lc, 1.6f * std::max(lamp, 0.25f));
    }
    if (ev & EV_SUGAR) {
        vec3 sp = {34.f, 0, 52.f}; sp.y = heightAt(sp.x, sp.z);
        mat4 m = translate(sp) * rotY(-0.7f);
        D.put(MESH_SHACK, m, {1, 1, 1}, 0.f);
        vec3 ch = xform(m, chimneyLocal[2]);
        u.chimney[2][0] = ch.x; u.chimney[2][1] = ch.y; u.chimney[2][2] = ch.z; u.chimney[2][3] = 1.f;
        addLight(xform(m, {0, 1.9f, -4.f}), 10.f, {1.0f, 0.55f, 0.2f}, 1.6f * std::max(lamp, 0.3f));
        // buckets on the first maples
        int nb = 0;
        for (auto& in : statics[MESH_MAPLE]) {
            if (nb >= 14) break;
            vec3 pos = {in.model[12], in.model[13], in.model[14]};
            float sc = in.model[0] > 0 ? in.model[0] : 10.f;
            if (pos.z > 120.f) continue;
            D.put(MESH_CYL, translate(pos + vec3(0.36f, 1.0f, 0.0f)) * scale({0.12f, 0.22f, 0.12f}), {0.7f, 0.72f, 0.78f}, 0.2f);
            D.put(MESH_CYL, translate(pos + vec3(0.0f, 0.0f, 0.0f)) * scale({0.001f, 0.001f, 0.001f}), {0, 0, 0}, 0.f);
            nb++;
        }
    }
}
