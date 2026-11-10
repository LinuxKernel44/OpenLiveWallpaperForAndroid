// Image loading (stb_image) and procedural foliage textures.
#include <android/asset_manager.h>
#include <cstring>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#include "common.h"
#include "renderer.h"

bool loadImageAsset(AAssetManager* am, const char* name, std::vector<uint8_t>& rgba, int& w, int& h) {
    AAsset* a = AAssetManager_open(am, name, AASSET_MODE_BUFFER);
    if (!a) return false;
    const void* data = AAsset_getBuffer(a);
    size_t n = (size_t)AAsset_getLength(a);
    int c;
    uint8_t* px = stbi_load_from_memory((const uint8_t*)data, (int)n, &w, &h, &c, 4);
    AAsset_close(a);
    if (!px) return false;
    rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    // resample non-square / non-1024 images to 1024x1024 (bilinear-ish nearest, textures tile anyway)
    const int T = 1024;
    if (w != T || h != T) {
        std::vector<uint8_t> out((size_t)T * T * 4);
        for (int y = 0; y < T; y++)
            for (int x = 0; x < T; x++) {
                int sx = x * w / T, sy = y * h / T;
                memcpy(&out[((size_t)y * T + x) * 4], &rgba[((size_t)sy * w + sx) * 4], 4);
            }
        rgba.swap(out); w = h = T;
    }
    return true;
}

static inline void putPx(uint8_t* img, int size, int x, int y, float r, float g, float b, float a) {
    x = ((x % size) + size) % size;
    y = ((y % size) + size) % size;
    uint8_t* p = img + ((size_t)y * size + x) * 4;
    float inv = 1.f - a;
    p[0] = (uint8_t)clampf(p[0] * inv + r * a, 0, 255);
    p[1] = (uint8_t)clampf(p[1] * inv + g * a, 0, 255);
    p[2] = (uint8_t)clampf(p[2] * inv + b * a, 0, 255);
    p[3] = 255;
}

void generateNeedleTexture(uint8_t* img, int size) {
    Rng rng(1234);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float n = fbm(x * 0.02f, y * 0.02f, 4, 5);
            float g = 40 + 50 * n;
            uint8_t* p = img + ((size_t)y * size + x) * 4;
            p[0] = (uint8_t)(g * 0.55f); p[1] = (uint8_t)(g * 1.05f); p[2] = (uint8_t)(g * 0.55f); p[3] = 255;
        }
    // needle strokes: many short lines, mostly drooping direction with spread
    for (int i = 0; i < 26000; i++) {
        float x = rng.range(0, (float)size), y = rng.range(0, (float)size);
        float ang = 1.5708f + rng.range(-1.1f, 1.1f) + (rng.next() < 0.5f ? 0.f : 3.14159f);
        float l = rng.range(14, 38);
        float shade = rng.range(0.55f, 1.35f);
        float gr = rng.range(0.0f, 1.0f);
        float cr = (40 + 30 * gr) * shade, cg = (95 + 60 * gr) * shade, cb = (42 + 20 * gr) * shade;
        float dx = std::cos(ang), dy = std::sin(ang);
        for (float t = 0; t < l; t += 0.7f) {
            float taper = 1.f - t / l * 0.6f;
            putPx(img, size, (int)(x + dx * t), (int)(y + dy * t), cr, cg, cb, 0.8f * taper);
        }
    }
    // twig lines (brownish) sparse
    for (int i = 0; i < 400; i++) {
        float x = rng.range(0, (float)size), y = rng.range(0, (float)size);
        float ang = rng.range(0, 6.28f), l = rng.range(40, 90);
        for (float t = 0; t < l; t += 0.8f) putPx(img, size, (int)(x + std::cos(ang) * t), (int)(y + std::sin(ang) * t), 52, 36, 24, 0.5f);
    }
}

void generateLeafCardTexture(uint8_t* img, int size) {
    memset(img, 0, (size_t)size * size * 4);
    Rng rng(777);
    // a branchlet cluster: twig with many leaves, drawn into a card with transparent surroundings
    for (int i = 0; i < 900; i++) {
        float px = rng.range(0.1f, 0.9f), py = rng.range(0.1f, 0.9f);
        // concentrate toward a blobby cluster
        float dx = px - 0.5f, dy = py - 0.5f;
        if (dx * dx + dy * dy > 0.17f) continue;
        float cx = px * size, cy = py * size;
        float ang = rng.range(0, 6.28f);
        float L = rng.range(22, 46), W = L * rng.range(0.38f, 0.55f);
        float shade = rng.range(0.55f, 1.0f);
        float ca = std::cos(ang), sa = std::sin(ang);
        int R = (int)(L + 2);
        for (int yy = -R; yy <= R; yy++)
            for (int xx = -R; xx <= R; xx++) {
                float u = xx * ca + yy * sa, v = -xx * sa + yy * ca;
                float uu = u / L, vv = v / W;
                // leaf shape: pointed ellipse
                float f = uu * uu + vv * vv * (1.0f + 0.9f * uu);
                if (f > 1.f) continue;
                float vein = std::fabs(vv) < 0.05f ? 0.8f : 1.0f;
                float s = shade * vein * (0.85f + 0.3f * (1.f - f));
                int X = (int)cx + xx, Y = (int)cy + yy;
                if (X < 0 || Y < 0 || X >= size || Y >= size) continue;
                uint8_t* p = img + ((size_t)Y * size + X) * 4;
                p[0] = p[1] = p[2] = (uint8_t)clampf(255 * s, 0, 255);
                p[3] = 255;
            }
    }
}
