#pragma once
#include <vector>
#include <cstdint>
#include "math.h"

struct Vertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float attr[4];  // x = material id, y = ambient occlusion, z = wind weight, w = aux (snow affinity / water u)
};
static_assert(sizeof(Vertex) == 48, "vertex size");

struct Instance {
    float model[16];
    float tint[4];
};
static_assert(sizeof(Instance) == 80, "instance size");

struct MeshData {
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
};

// Material ids (texture array layers 0..12, then specials)
enum Mat {
    M_SNOW = 0, M_GRASS, M_FOREST, M_ROCK, M_WOOD, M_ROOF, M_BARK, M_GRAVEL, M_LEAVES, M_MUD, M_ICE, M_NEEDLE, M_LEAFCARD,
    M_TERRAIN = 100, M_WATER = 101, M_EMISSIVE = 102, M_SOLID = 103, M_FUR = 104, M_FLAG_QC = 105, M_FLAG_CA = 106, M_FABRIC = 107, M_METAL = 108
};
static const int TEX_LAYERS = 13;

// Mesh ids
enum MeshId {
    MESH_TERRAIN = 0, MESH_FIR_NEAR, MESH_FIR_MID, MESH_FIR_FAR, MESH_BIRCH, MESH_MAPLE, MESH_CABIN, MESH_CABIN_SMALL,
    MESH_SPHERE, MESH_BOX, MESH_CYL, MESH_CONE, MESH_ROCK, MESH_BRIDGE, MESH_LOGS, MESH_FENCE, MESH_LAMP, MESH_SNOWMAN,
    MESH_FLAGPOLE, MESH_FLAG, MESH_HEART, MESH_SHACK, MESH_STUMP, MESH_BUSH, MESH_GRASS_TUFT, MESH_REED, MESH_FIR_DECOR,
    MESH_WATER, MESH_COUNT
};

struct Draw {
    int mesh;
    uint32_t firstInstance;
    uint32_t count;
    uint32_t shadowCount;
};

// Frame uniform buffer (std140-compatible: only vec4 / mat4 members)
struct FrameUBO {
    float viewProj[16];
    float lightVP[16];
    float camPos[4];     // xyz, time
    float sunDir[4];     // xyz toward sun, w = sun elevation
    float sunColor[4];   // rgb, a = shadow strength
    float moonDir[4];    // xyz, w = phase
    float moonColor[4];
    float skyZenith[4];
    float skyHorizon[4];
    float skyAmbient[4];     // rgb, a = night factor
    float groundAmbient[4];
    float fogColor[4];       // rgb, a = density
    float w0[4];  // season, snow, autumn, leaf
    float w1[4];  // cloud, rain, snowfall, fog
    float w2[4];  // wind, lightning, ice, aurora
    float w3[4];  // event mask, -, flow, exposure
    float camRight[4];  // xyz, tanHalfFovY
    float camUp[4];     // xyz, aspect
    float camFwd[4];
    float lightPos[12][4];  // xyz, radius
    float lightCol[12][4];  // rgb, intensity
    float chimney[4][4];    // xyz, active
};

enum Param {
    P_TIME = 0, P_SUNX, P_SUNY, P_SUNZ, P_MOONX, P_MOONY, P_MOONZ, P_MOONPHASE, P_SEASON, P_SNOW, P_AUTUMN, P_LEAF,
    P_CLOUD, P_RAIN, P_SNOWFALL, P_FOG, P_WIND, P_LIGHTNING, P_ICE, P_AURORA, P_EVENT, P_TEMP, P_SCROLL, P_QUALITY,
    P_DAYOFYEAR, P_HOUR, P_FLOW, P_DT, P_COUNT = 32
};

enum Event {
    EV_CHRISTMAS = 1, EV_NEWYEAR = 2, EV_QUEBEC = 4, EV_CANADA = 8, EV_HALLOWEEN = 16, EV_EASTER = 32, EV_THANKS = 64,
    EV_VALENTINE = 128, EV_CARNAVAL = 256, EV_STPATRICK = 512, EV_SUGAR = 1024, EV_FIREWORKS = 2048
};

struct SceneParams {
    float p[P_COUNT];
    int ev() const { return (int)(p[P_EVENT] + 0.5f); }
};

// Camera shared between scene (needs it for particles/LOD) and renderer
struct Camera {
    vec3 eye, right, up, fwd;
    float fovY, aspect;
};

// Generates the whole world. Implemented in scene.cpp.
class Scene {
public:
    void build(int quality);
    float heightAt(float x, float z) const;
    // Fills per-frame dynamic draws (animals, people, event props). Instances appended to out per mesh.
    void update(const SceneParams& sp, std::vector<Instance> dyn[MESH_COUNT], FrameUBO& ubo);
    vec3 cameraPos(float scroll, float t) const;

    MeshData meshes[MESH_COUNT];
    std::vector<Instance> statics[MESH_COUNT];
    bool shadowCaster[MESH_COUNT];
    uint32_t shadowCount[MESH_COUNT];
    vec3 shadowCenter;

    float waterLevel(float z) const;
    float streamX(float z) const;
    float forestDensity(float x, float z) const;
    float rinkBlend(float x, float z) const;
    static constexpr float RINK_X = -27.f, RINK_Z = -6.f;
    float rinkH = 0.f;

private:
    float baseHeight(float x, float z) const;
    float rawHeight(float x, float z) const;
    struct Cabin { vec3 pos; float yaw; int type; };
    std::vector<Cabin> cabins;
    std::vector<vec3> chimneys;
    std::vector<vec3> lampPos;
    vec3 chimneyLocal[3];
    vec3 campfire;
    int quality_ = 1;
};

// Texture/image generation (textures.cpp)
void generateNeedleTexture(uint8_t* rgba, int size);
void generateLeafCardTexture(uint8_t* rgba, int size);
