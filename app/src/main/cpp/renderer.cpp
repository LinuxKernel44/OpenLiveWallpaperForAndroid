// Vulkan renderer: shadow pass, lit scene, water, sky, particles.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>
#include "renderer.h"

#define TAG "OLW"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define VK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { LOGE("Vulkan error %d at %s:%d (%s)", (int)_r, __FILE__, __LINE__, #call); return false; } } while (0)
#define VKV(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { LOGE("Vulkan error %d at %s:%d (%s)", (int)_r, __FILE__, __LINE__, #call); } } while (0)

static const int FRAMES = 2;
static const int TEX_SIZE = 1024;
static const int MAX_DYN = 6000;
static const char* LAYER_NAMES[TEX_LAYERS] = {"snow", "grass", "forest", "rock", "wood", "roof", "bark", "gravel", "leaves", "mud", "ice", nullptr, nullptr};

struct Buf { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; void* map = nullptr; size_t size = 0; };
struct Img { VkImage i = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; VkImageView v = VK_NULL_HANDLE; };

class Renderer {
public:
    AAssetManager* am = nullptr;
    int quality = 1;
    bool ready = false;

    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps;
    VkPhysicalDeviceProperties devProps;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    // surface-dependent
    ANativeWindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    VkFormat scFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D ext{};
    std::vector<VkImage> scImages;
    std::vector<VkImageView> scViews;
    std::vector<VkFramebuffer> fbs;
    std::vector<VkSemaphore> renderSems;
    Img depth, msaaColor, offImg[FRAMES];
    VkExtent2D rext{};
    float renderScale = 0.65f;
    VkRenderPass mainRP = VK_NULL_HANDLE;
    VkPipeline pLit = VK_NULL_HANDLE, pWater = VK_NULL_HANDLE, pSky = VK_NULL_HANDLE, pPartA = VK_NULL_HANDLE, pPartAdd = VK_NULL_HANDLE;

    // persistent
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmds[FRAMES];
    VkFence fences[FRAMES];
    VkSemaphore acquireSems[FRAMES];
    int frame = 0;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl0 = VK_NULL_HANDLE, dsl1 = VK_NULL_HANDLE;
    VkPipelineLayout plLayout = VK_NULL_HANDLE;
    VkDescriptorSet set0[FRAMES], set1;
    Buf ubo[FRAMES], vbuf, ibuf, sbuf, dbuf[FRAMES];
    Img albedo, normalTex, shadow;
    VkSampler texSampler = VK_NULL_HANDLE, shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRP = VK_NULL_HANDLE;
    VkFramebuffer shadowFB = VK_NULL_HANDLE;
    VkPipeline pShadow = VK_NULL_HANDLE;
    int shadowSize = 2048;
    VkShaderModule modules[16];
    int nModules = 0;

    Scene scene;
    uint32_t baseVertex[MESH_COUNT], firstIndex[MESH_COUNT], indexCount[MESH_COUNT];
    uint32_t staticFirst[MESH_COUNT];
    std::vector<Draw> staticDraws;
    std::vector<Instance> dyn[MESH_COUNT];
    float lastTime = 0;

    bool init();
    void shutdown();
    bool setSurface(ANativeWindow* w, int width, int height);
    void destroySwapchain();
    bool createSwapchain();
    bool createMainRP();
    bool createPipelines();
    void draw(const float* params, int n);

    uint32_t findMem(uint32_t bits, VkMemoryPropertyFlags f) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & f) == f) return i;
        return 0;
    }
    bool makeBuffer(Buf& b, size_t size, VkBufferUsageFlags usage, bool map = true) {
        b.size = size;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK(vkCreateBuffer(dev, &bi, nullptr, &b.b));
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, b.b, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK(vkAllocateMemory(dev, &ai, nullptr, &b.m));
        VK(vkBindBufferMemory(dev, b.b, b.m, 0));
        if (map) VK(vkMapMemory(dev, b.m, 0, VK_WHOLE_SIZE, 0, &b.map));
        return true;
    }
    void freeBuf(Buf& b) {
        if (b.map) vkUnmapMemory(dev, b.m);
        if (b.b) vkDestroyBuffer(dev, b.b, nullptr);
        if (b.m) vkFreeMemory(dev, b.m, nullptr);
        b = Buf();
    }
    bool makeImage(Img& im, uint32_t w, uint32_t h, uint32_t mips, uint32_t layers, VkFormat fmt, VkImageUsageFlags usage,
                   VkSampleCountFlagBits s, VkImageAspectFlags aspect, bool array) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w, h, 1}; ci.mipLevels = mips; ci.arrayLayers = layers;
        ci.samples = s; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK(vkCreateImage(dev, &ci, nullptr, &im.i));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, im.i, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkAllocateMemory(dev, &ai, nullptr, &im.m));
        VK(vkBindImageMemory(dev, im.i, im.m, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = im.i; vi.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = {aspect, 0, mips, 0, layers};
        VK(vkCreateImageView(dev, &vi, nullptr, &im.v));
        return true;
    }
    void freeImg(Img& im) {
        if (im.v) vkDestroyImageView(dev, im.v, nullptr);
        if (im.i) vkDestroyImage(dev, im.i, nullptr);
        if (im.m) vkFreeMemory(dev, im.m, nullptr);
        im = Img();
    }
    VkCommandBuffer beginOneShot() {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer c; vkAllocateCommandBuffers(dev, &ai, &c);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(c, &bi);
        return c;
    }
    void endOneShot(VkCommandBuffer c) {
        vkEndCommandBuffer(c);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &c;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(dev, pool, 1, &c);
    }
    VkShaderModule shader(const char* name) {
        AAsset* a = AAssetManager_open(am, name, AASSET_MODE_BUFFER);
        if (!a) { LOGE("missing shader %s", name); return VK_NULL_HANDLE; }
        size_t n = AAsset_getLength(a);
        std::vector<uint32_t> code((n + 3) / 4);
        memcpy(code.data(), AAsset_getBuffer(a), n);
        AAsset_close(a);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = n; ci.pCode = code.data();
        VkShaderModule m;
        if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
        modules[nModules++] = m;
        return m;
    }
    bool uploadTextures();
    bool uploadGeometry();
    bool createShadow();
    void fillFrame(FrameUBO& u, const float* p, const Camera& cam);
};

// ---------- environment / palette ----------
struct Key { float e; vec3 zen, hor; };
static vec3 mix3(vec3 a, vec3 b, float t) { return a * (1 - t) + b * t; }
static vec3 mul3(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

static void skyPalette(float e, vec3& zen, vec3& hor) {
    static const Key K[] = {
        {-0.30f, {0.002f, 0.005f, 0.018f}, {0.008f, 0.014f, 0.035f}},
        {-0.12f, {0.012f, 0.025f, 0.09f}, {0.07f, 0.07f, 0.16f}},
        {-0.03f, {0.04f, 0.07f, 0.22f}, {0.55f, 0.22f, 0.20f}},
        {0.03f, {0.08f, 0.14f, 0.38f}, {1.05f, 0.42f, 0.16f}},
        {0.12f, {0.12f, 0.26f, 0.62f}, {0.95f, 0.66f, 0.42f}},
        {0.35f, {0.13f, 0.32f, 0.78f}, {0.62f, 0.76f, 0.98f}},
        {1.00f, {0.10f, 0.28f, 0.80f}, {0.56f, 0.72f, 0.98f}},
    };
    const int n = sizeof(K) / sizeof(K[0]);
    if (e <= K[0].e) { zen = K[0].zen; hor = K[0].hor; return; }
    for (int i = 0; i < n - 1; i++)
        if (e <= K[i + 1].e) {
            float t = (e - K[i].e) / (K[i + 1].e - K[i].e);
            t = t * t * (3 - 2 * t);
            zen = mix3(K[i].zen, K[i + 1].zen, t); hor = mix3(K[i].hor, K[i + 1].hor, t);
            return;
        }
    zen = K[n - 1].zen; hor = K[n - 1].hor;
}

static void setv(float* d, float a, float b, float c, float w) { d[0] = a; d[1] = b; d[2] = c; d[3] = w; }

void Renderer::fillFrame(FrameUBO& u, const float* p, const Camera& cam) {
    memset(&u, 0, sizeof(u));
    vec3 S = norm(vec3(p[P_SUNX], p[P_SUNY], p[P_SUNZ]));
    vec3 M = norm(vec3(p[P_MOONX], p[P_MOONY], p[P_MOONZ]));
    float e = S.y;
    float cloud = clampf(p[P_CLOUD], 0, 1), rain = clampf(p[P_RAIN], 0, 1), fogAmt = clampf(p[P_FOG], 0, 1);
    float snowfall = clampf(p[P_SNOWFALL], 0, 1);
    float dayf = smoothstepf(-0.06f, 0.3f, e) ;
    float night = 1.f - smoothstepf(-0.30f, -0.02f, e);
    float illum = 0.5f - 0.5f * std::cos(p[P_MOONPHASE] * 6.28318f);
    float moonUp = smoothstepf(-0.05f, 0.12f, M.y);

    vec3 zen, hor;
    skyPalette(e, zen, hor);
    float overcast = clampf(cloud * 0.9f + rain * 0.3f + snowfall * 0.3f, 0, 1);
    vec3 grayDay = vec3(0.34f, 0.38f, 0.44f) * (0.15f + 0.85f * dayf) + vec3(0.01f, 0.012f, 0.02f);
    vec3 grayHor = vec3(0.50f, 0.54f, 0.60f) * (0.12f + 0.88f * dayf) * (1.f - 0.25f * rain) + vec3(0.012f, 0.014f, 0.02f);
    // keep twilight colours under overcast, only partly
    float twil = 1.f - std::fabs(e * 6.0f); if (twil < 0) twil = 0;
    zen = mix3(zen, grayDay, overcast * 0.85f);
    hor = mix3(hor, mix3(grayHor, hor * 0.7f, twil * 0.5f), overcast * 0.85f);
    // moonlit night sky lift
    zen = zen + vec3(0.004f, 0.007f, 0.016f) * illum * moonUp * night;
    hor = hor + vec3(0.01f, 0.014f, 0.028f) * illum * moonUp * night;

    // sun radiance through atmosphere
    float air = 1.f / std::max(e, 0.04f); if (air > 14) air = 14;
    vec3 tr = vec3(std::exp(-0.05f * air * 0.9f), std::exp(-0.11f * air * 0.9f), std::exp(-0.25f * air * 0.9f));
    float sunFade = smoothstepf(-0.035f, 0.03f, e);
    vec3 sunCol = tr * 3.4f * sunFade;
    sunCol = sunCol * (1.f - 0.85f * std::pow(overcast, 1.3f));
    float shadowStr = (1.f - 0.92f * overcast) * sunFade;

    vec3 moonCol = vec3(0.34f, 0.45f, 0.70f) * (0.12f + 0.7f * illum) * moonUp * (1.f - 0.7f * overcast);
    moonCol = moonCol * night;

    vec3 amb = mix3(zen, hor, 0.45f) * (0.55f + 0.2f * overcast);
    amb = amb + vec3(0.03f, 0.045f, 0.095f) * night * (0.55f + illum * moonUp * 1.2f);
    float snow = clampf(p[P_SNOW], 0, 1);
    vec3 ground = mul3(amb, vec3(0.8f, 0.85f, 0.9f)) * 0.55f + sunCol * std::max(e, 0.f) * (0.10f + 0.18f * snow);
    ground = mix3(ground, ground * vec3(1.0f, 1.04f, 1.12f), snow);

    vec3 fog = mix3(hor, mix3(amb * 1.2f, hor, 0.4f), 0.3f);
    float fogDensity = 0.00045f + 0.0007f * overcast + 0.0007f * snowfall;
    // exposure
    float exposure = mixf(1.7f, 0.95f, dayf);

    float lampOn = 1.f - smoothstepf(0.0f, 0.28f, e);
    lampOn = std::max(lampOn, overcast * 0.35f * dayf);

    mat4 view = viewFromBasis(cam.eye, cam.right, cam.up, cam.fwd);
    mat4 proj = perspectiveRevZ(cam.fovY, cam.aspect, 0.4f, 6000.f);
    mat4 vp = proj * view;
    memcpy(u.viewProj, vp.m, 64);

    // light view (shadow)
    vec3 center = scene.shadowCenter;
    vec3 L = e > 0.02f ? S : M;
    L = norm(L);
    float half = quality == 0 ? 80.f : 98.f;
    vec3 lf = L * -1.f;
    vec3 lupv = std::fabs(lf.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 lr = norm(cross(lupv, lf));
    vec3 lu = cross(lf, lr);
    vec3 leye = center + L * 220.f;
    // snap to texel grid to avoid shimmering
    mat4 lview = viewFromBasis(leye, lr, lu, lf);
    mat4 lproj = orthoLH(-half, half, -half, half, 0.f, 520.f);
    mat4 lvp = lproj * lview;
    memcpy(u.lightVP, lvp.m, 64);

    setv(u.camPos, cam.eye.x, cam.eye.y, cam.eye.z, p[P_TIME]);
    setv(u.sunDir, S.x, S.y, S.z, e);
    setv(u.sunColor, sunCol.x, sunCol.y, sunCol.z, shadowStr);
    setv(u.moonDir, M.x, M.y, M.z, p[P_MOONPHASE]);
    setv(u.moonColor, moonCol.x, moonCol.y, moonCol.z, 1);
    setv(u.skyZenith, zen.x, zen.y, zen.z, 1);
    setv(u.skyHorizon, hor.x, hor.y, hor.z, 1);
    setv(u.skyAmbient, amb.x, amb.y, amb.z, night);
    setv(u.groundAmbient, ground.x, ground.y, ground.z, 1);
    setv(u.fogColor, fog.x, fog.y, fog.z, fogDensity);
    setv(u.w0, p[P_SEASON], snow, clampf(p[P_AUTUMN], 0, 1), clampf(p[P_LEAF], 0, 1));
    setv(u.w1, cloud, rain, snowfall, fogAmt);
    setv(u.w2, clampf(p[P_WIND], 0, 1.5f), p[P_LIGHTNING], clampf(p[P_ICE], 0, 1), clampf(p[P_AURORA], 0, 1));
    setv(u.w3, p[P_EVENT], lampOn, clampf(p[P_FLOW], 0, 1), exposure);
    setv(u.camRight, cam.right.x, cam.right.y, cam.right.z, std::tan(cam.fovY * 0.5f));
    setv(u.camUp, cam.up.x, cam.up.y, cam.up.z, cam.aspect);
    setv(u.camFwd, cam.fwd.x, cam.fwd.y, cam.fwd.z, 0);
}

// ---------- init ----------
bool Renderer::init() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "OpenLiveWallpaper"; app.apiVersion = VK_API_VERSION_1_1;
    const char* iext[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ic.pApplicationInfo = &app; ic.enabledExtensionCount = 2; ic.ppEnabledExtensionNames = iext;
    VK(vkCreateInstance(&ic, nullptr, &inst));
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (!n) { LOGE("no vulkan device"); return false; }
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(inst, &n, pds.data());
    phys = pds[0];
    vkGetPhysicalDeviceProperties(phys, &devProps);
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    LOGI("GPU: %s api %u.%u", devProps.deviceName, VK_VERSION_MAJOR(devProps.apiVersion), VK_VERSION_MINOR(devProps.apiVersion));
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp.data());
    qfam = 0;
    for (uint32_t i = 0; i < qn; i++) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; }
    float prio = 1.f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = qfam; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures feat{}, avail{};
    vkGetPhysicalDeviceFeatures(phys, &avail);
    feat.samplerAnisotropy = avail.samplerAnisotropy;
    const char* dext[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &qi; dc.enabledExtensionCount = 1; dc.ppEnabledExtensionNames = dext;
    dc.pEnabledFeatures = &feat;
    VK(vkCreateDevice(phys, &dc, nullptr, &dev));
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    // MSAA choice
    VkSampleCountFlags sup = devProps.limits.framebufferColorSampleCounts & devProps.limits.framebufferDepthSampleCounts;
    samples = VK_SAMPLE_COUNT_1_BIT;
    if (quality >= 2 && (sup & VK_SAMPLE_COUNT_4_BIT)) samples = VK_SAMPLE_COUNT_4_BIT;

    shadowSize = quality == 0 ? 1024 : 2048;
    renderScale = quality == 0 ? 0.5f : (quality == 1 ? 0.65f : 0.85f);

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = qfam;
    VK(vkCreateCommandPool(dev, &pi, nullptr, &pool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = FRAMES;
    VK(vkAllocateCommandBuffers(dev, &ai, cmds));
    for (int i = 0; i < FRAMES; i++) {
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK(vkCreateFence(dev, &fi, nullptr, &fences[i]));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK(vkCreateSemaphore(dev, &si, nullptr, &acquireSems[i]));
    }

    // descriptor layouts
    VkDescriptorSetLayoutBinding b0{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo l0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    l0.bindingCount = 1; l0.pBindings = &b0;
    VK(vkCreateDescriptorSetLayout(dev, &l0, nullptr, &dsl0));
    VkDescriptorSetLayoutBinding b1[3];
    for (int i = 0; i < 3; i++) b1[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo l1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    l1.bindingCount = 3; l1.pBindings = b1;
    VK(vkCreateDescriptorSetLayout(dev, &l1, nullptr, &dsl1));
    VkDescriptorSetLayout layouts[2] = {dsl0, dsl1};
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 2; pl.pSetLayouts = layouts; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pcr;
    VK(vkCreatePipelineLayout(dev, &pl, nullptr, &plLayout));
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = FRAMES + 1; dp.poolSizeCount = 2; dp.pPoolSizes = ps;
    VK(vkCreateDescriptorPool(dev, &dp, nullptr, &dpool));
    for (int i = 0; i < FRAMES; i++) {
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = dpool; da.descriptorSetCount = 1; da.pSetLayouts = &dsl0;
        VK(vkAllocateDescriptorSets(dev, &da, &set0[i]));
        if (!makeBuffer(ubo[i], sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) return false;
        if (!makeBuffer(dbuf[i], sizeof(Instance) * MAX_DYN, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
        VkDescriptorBufferInfo bi{ubo[i].b, 0, sizeof(FrameUBO)};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set0[i]; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    }
    VkDescriptorSetAllocateInfo da1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da1.descriptorPool = dpool; da1.descriptorSetCount = 1; da1.pSetLayouts = &dsl1;
    VK(vkAllocateDescriptorSets(dev, &da1, &set1));

    // scene + resources
    scene.build(quality);
    if (!uploadGeometry()) return false;
    if (!uploadTextures()) return false;
    if (!createShadow()) return false;
    VkDescriptorImageInfo ii[3];
    ii[0] = {texSampler, albedo.v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    ii[1] = {texSampler, normalTex.v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    ii[2] = {shadowSampler, shadow.v, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet ws[3];
    for (int i = 0; i < 3; i++) {
        ws[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        ws[i].dstSet = set1; ws[i].dstBinding = i; ws[i].descriptorCount = 1;
        ws[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(dev, 3, ws, 0, nullptr);
    LOGI("renderer init ok (quality %d, samples %d, shadow %d)", quality, (int)samples, shadowSize);
    return true;
}

bool Renderer::uploadGeometry() {
    size_t nv = 0, ni = 0, ns = 0;
    for (int m = 0; m < MESH_COUNT; m++) { nv += scene.meshes[m].v.size(); ni += scene.meshes[m].i.size(); ns += scene.statics[m].size(); }
    if (!makeBuffer(vbuf, std::max<size_t>(nv, 1) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
    if (!makeBuffer(ibuf, std::max<size_t>(ni, 1) * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
    if (!makeBuffer(sbuf, std::max<size_t>(ns, 1) * sizeof(Instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
    size_t vo = 0, io = 0, so = 0;
    for (int m = 0; m < MESH_COUNT; m++) {
        MeshData& md = scene.meshes[m];
        baseVertex[m] = (uint32_t)vo; firstIndex[m] = (uint32_t)io; indexCount[m] = (uint32_t)md.i.size();
        if (!md.v.empty()) memcpy((Vertex*)vbuf.map + vo, md.v.data(), md.v.size() * sizeof(Vertex));
        if (!md.i.empty()) memcpy((uint32_t*)ibuf.map + io, md.i.data(), md.i.size() * 4);
        vo += md.v.size(); io += md.i.size();
        staticFirst[m] = (uint32_t)so;
        if (!scene.statics[m].empty()) {
            memcpy((Instance*)sbuf.map + so, scene.statics[m].data(), scene.statics[m].size() * sizeof(Instance));
            staticDraws.push_back({m, (uint32_t)so, (uint32_t)scene.statics[m].size(), scene.shadowCount[m]});
        }
        so += scene.statics[m].size();
    }
    LOGI("geometry: %zu verts, %zu indices, %zu instances", nv, ni, ns);
    return true;
}

bool Renderer::uploadTextures() {
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR; sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod = 12; sci.anisotropyEnable = devProps.limits.maxSamplerAnisotropy >= 4 ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy = quality >= 2 ? 4.f : (quality == 1 ? 2.f : 1.f);
    VK(vkCreateSampler(dev, &sci, nullptr, &texSampler));

    const uint32_t mips = 11;
    const size_t layerBytes = (size_t)TEX_SIZE * TEX_SIZE * 4;
    for (int pass = 0; pass < 2; pass++) {
        Img& im = pass == 0 ? albedo : normalTex;
        VkFormat fmt = pass == 0 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        if (!makeImage(im, TEX_SIZE, TEX_SIZE, mips, TEX_LAYERS, fmt,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, true)) return false;
        Buf st;
        if (!makeBuffer(st, layerBytes * TEX_LAYERS, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
        for (int l = 0; l < TEX_LAYERS; l++) {
            uint8_t* dst = (uint8_t*)st.map + layerBytes * l;
            std::vector<uint8_t> px; int w = 0, h = 0;
            bool ok = false;
            if (LAYER_NAMES[l]) {
                char name[96]; snprintf(name, sizeof(name), "tex/%s_%c.jpg", LAYER_NAMES[l], pass == 0 ? 'c' : 'n');
                ok = loadImageAsset(am, name, px, w, h) && w == TEX_SIZE && h == TEX_SIZE;
                if (!ok) LOGE("texture %s failed", name);
            }
            if (ok) memcpy(dst, px.data(), layerBytes);
            else if (l == 11 && pass == 0) generateNeedleTexture(dst, TEX_SIZE);
            else if (l == 12 && pass == 0) generateLeafCardTexture(dst, TEX_SIZE);
            else if (pass == 1) { for (size_t i = 0; i < layerBytes; i += 4) { dst[i] = 128; dst[i + 1] = 128; dst[i + 2] = 255; dst[i + 3] = 255; } }
            else { for (size_t i = 0; i < layerBytes; i += 4) { dst[i] = dst[i + 1] = dst[i + 2] = 128; dst[i + 3] = 255; } }
        }
        VkCommandBuffer c = beginOneShot();
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image = im.i; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, TEX_LAYERS};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        std::vector<VkBufferImageCopy> regions(TEX_LAYERS);
        for (int l = 0; l < TEX_LAYERS; l++) {
            regions[l] = {};
            regions[l].bufferOffset = layerBytes * l;
            regions[l].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)l, 1};
            regions[l].imageExtent = {(uint32_t)TEX_SIZE, (uint32_t)TEX_SIZE, 1};
        }
        vkCmdCopyBufferToImage(c, st.b, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, TEX_LAYERS, regions.data());
        int32_t mw = TEX_SIZE, mh = TEX_SIZE;
        for (uint32_t m = 1; m < mips; m++) {
            VkImageMemoryBarrier pb = b;
            pb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1, 0, TEX_LAYERS};
            pb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; pb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            pb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; pb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &pb);
            VkImageBlit bl{};
            bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, TEX_LAYERS};
            bl.srcOffsets[1] = {mw, mh, 1};
            int32_t nw = std::max(1, mw / 2), nh = std::max(1, mh / 2);
            bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, TEX_LAYERS};
            bl.dstOffsets[1] = {nw, nh, 1};
            vkCmdBlitImage(c, im.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im.i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            pb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; pb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            pb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; pb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &pb);
            mw = nw; mh = nh;
        }
        VkImageMemoryBarrier lb = b;
        lb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1, 0, TEX_LAYERS};
        lb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; lb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; lb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &lb);
        endOneShot(c);
        freeBuf(st);
    }
    return true;
}

bool Renderer::createShadow() {
    if (!makeImage(shadow, shadowSize, shadowSize, 1, 1, VK_FORMAT_D32_SFLOAT,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, false))
        return false;
    VkAttachmentDescription ad{};
    ad.format = VK_FORMAT_D32_SFLOAT; ad.samples = VK_SAMPLE_COUNT_1_BIT;
    ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; ad.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.pDepthStencilAttachment = &ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1; rp.pAttachments = &ad; rp.subpassCount = 1; rp.pSubpasses = &sp; rp.dependencyCount = 2; rp.pDependencies = deps;
    VK(vkCreateRenderPass(dev, &rp, nullptr, &shadowRP));
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = shadowRP; fi.attachmentCount = 1; fi.pAttachments = &shadow.v; fi.width = fi.height = shadowSize; fi.layers = 1;
    VK(vkCreateFramebuffer(dev, &fi, nullptr, &shadowFB));
    VkSamplerCreateInfo sc2{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc2.magFilter = sc2.minFilter = VK_FILTER_LINEAR; sc2.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sc2.addressModeU = sc2.addressModeV = sc2.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc2.compareEnable = VK_TRUE; sc2.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VK(vkCreateSampler(dev, &sc2, nullptr, &shadowSampler));
    // initial layout transition so it is valid to sample before the first shadow pass
    VkCommandBuffer c = beginOneShot();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = shadow.i; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    endOneShot(c);
    return true;
}

// ---------- surface-dependent ----------
bool Renderer::createMainRP() {
    bool ms = samples != VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentDescription ad[3]{};
    ad[0].format = scFormat; ad[0].samples = samples; ad[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ad[0].storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    ad[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ad[0].finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ad[1].format = VK_FORMAT_D32_SFLOAT; ad[1].samples = samples; ad[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ad[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; ad[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ad[2].format = scFormat; ad[2].samples = VK_SAMPLE_COUNT_1_BIT; ad[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; ad[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        rref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &cref;
    sp.pDepthStencilAttachment = &dref; sp.pResolveAttachments = ms ? &rref : nullptr;
    VkSubpassDependency dep[2]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep[0].dstStageMask = dep[0].srcStageMask;
    dep[0].srcAccessMask = 0;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dep[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    VkAttachmentDescription ms3[3] = {ad[0], ad[1], ad[2]};
    VkAttachmentDescription s2[2] = {ad[0], ad[1]};
    if (ms) { rp.attachmentCount = 3; rp.pAttachments = ms3; }
    else { rp.attachmentCount = 2; rp.pAttachments = s2; }
    rp.subpassCount = 1; rp.pSubpasses = &sp; rp.dependencyCount = 2; rp.pDependencies = dep;
    VK(vkCreateRenderPass(dev, &rp, nullptr, &mainRP));
    return true;
}

static VkPipelineShaderStageCreateInfo stage(VkShaderStageFlagBits s, VkShaderModule m) {
    VkPipelineShaderStageCreateInfo i{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    i.stage = s; i.module = m; i.pName = "main";
    return i;
}

bool Renderer::createPipelines() {
    static VkShaderModule mLitV, mLitF, mWaterF, mSkyV, mSkyF, mPartV, mPartF, mShadowV;
    if (!mLitV) {
        mLitV = shader("shaders/lit.vert.spv"); mLitF = shader("shaders/lit.frag.spv"); mWaterF = shader("shaders/water.frag.spv");
        mSkyV = shader("shaders/sky.vert.spv"); mSkyF = shader("shaders/sky.frag.spv");
        mPartV = shader("shaders/particles.vert.spv"); mPartF = shader("shaders/particles.frag.spv");
        mShadowV = shader("shaders/shadow.vert.spv");
        if (!mLitV || !mLitF || !mWaterF || !mSkyV || !mSkyF || !mPartV || !mPartF || !mShadowV) return false;
    }
    VkVertexInputBindingDescription bind[2] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription attr[9] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16}, {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
        {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48}, {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 2; vin.pVertexBindingDescriptions = bind; vin.vertexAttributeDescriptionCount = 9; vin.pVertexAttributeDescriptions = attr;
    VkPipelineVertexInputStateCreateInfo vinEmpty{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1; vs.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineMultisampleStateCreateInfo ms1{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms1.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dsOn{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dsOn.depthTestEnable = VK_TRUE; dsOn.depthWriteEnable = VK_TRUE; dsOn.depthCompareOp = VK_COMPARE_OP_GREATER;
    VkPipelineDepthStencilStateCreateInfo dsNoWrite = dsOn; dsNoWrite.depthWriteEnable = VK_FALSE;
    VkPipelineDepthStencilStateCreateInfo dsSky = dsNoWrite; dsSky.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cbOff{}; cbOff.colorWriteMask = 0xF;
    VkPipelineColorBlendAttachmentState cbA = cbOff;
    cbA.blendEnable = VK_TRUE; cbA.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cbA.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbA.colorBlendOp = VK_BLEND_OP_ADD; cbA.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cbA.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cbA.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendAttachmentState cbAdd = cbA; cbAdd.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    auto blendState = [](VkPipelineColorBlendAttachmentState* a) {
        VkPipelineColorBlendStateCreateInfo c{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        c.attachmentCount = 1; c.pAttachments = a; return c;
    };
    VkDynamicState dynS[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynS;

    auto make = [&](VkShaderModule v, VkShaderModule f, const VkPipelineVertexInputStateCreateInfo* vi, const VkPipelineDepthStencilStateCreateInfo* ds,
                    VkPipelineColorBlendAttachmentState* cb, VkRenderPass rp, const VkPipelineMultisampleStateCreateInfo* msi,
                    const VkPipelineRasterizationStateCreateInfo* rst, VkPipeline* out) -> bool {
        VkPipelineShaderStageCreateInfo st[2] = {stage(VK_SHADER_STAGE_VERTEX_BIT, v), stage(VK_SHADER_STAGE_FRAGMENT_BIT, f)};
        VkPipelineColorBlendStateCreateInfo bs = blendState(cb);
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = f ? 2 : 1; gi.pStages = st; gi.pVertexInputState = vi; gi.pInputAssemblyState = &ia; gi.pViewportState = &vs;
        gi.pRasterizationState = rst; gi.pMultisampleState = msi; gi.pDepthStencilState = ds;
        gi.pColorBlendState = f ? &bs : nullptr; gi.pDynamicState = &dyn; gi.layout = plLayout; gi.renderPass = rp; gi.subpass = 0;
        VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gi, nullptr, out));
        return true;
    };
    if (!make(mLitV, mLitF, &vin, &dsOn, &cbOff, mainRP, &ms, &rs, &pLit)) return false;
    if (!make(mLitV, mWaterF, &vin, &dsNoWrite, &cbA, mainRP, &ms, &rs, &pWater)) return false;
    if (!make(mSkyV, mSkyF, &vinEmpty, &dsSky, &cbOff, mainRP, &ms, &rs, &pSky)) return false;
    if (!make(mPartV, mPartF, &vinEmpty, &dsNoWrite, &cbA, mainRP, &ms, &rs, &pPartA)) return false;
    if (!make(mPartV, mPartF, &vinEmpty, &dsNoWrite, &cbAdd, mainRP, &ms, &rs, &pPartAdd)) return false;
    if (!pShadow) {
        VkPipelineRasterizationStateCreateInfo rsS = rs;
        rsS.depthBiasEnable = VK_TRUE; rsS.depthBiasConstantFactor = 1.5f; rsS.depthBiasSlopeFactor = 2.0f;
        VkPipelineDepthStencilStateCreateInfo dsS{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dsS.depthTestEnable = VK_TRUE; dsS.depthWriteEnable = VK_TRUE; dsS.depthCompareOp = VK_COMPARE_OP_LESS;
        if (!make(mShadowV, VK_NULL_HANDLE, &vin, &dsS, &cbOff, shadowRP, &ms1, &rsS, &pShadow)) return false;
    }
    return true;
}

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps));
    VkBool32 sup = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(phys, qfam, surface, &sup);
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fn, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) { chosen = f; break; }
    scFormat = chosen.format;
    ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFF) { ext.width = ANativeWindow_getWidth(window); ext.height = ANativeWindow_getHeight(window); }
    uint32_t imgCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
    VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    si.surface = surface; si.minImageCount = imgCount; si.imageFormat = scFormat; si.imageColorSpace = chosen.colorSpace;
    si.imageExtent = ext; si.imageArrayLayers = 1; si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; si.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) si.preTransform = caps.currentTransform;
    si.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    si.presentMode = VK_PRESENT_MODE_FIFO_KHR; si.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(dev, &si, nullptr, &sc));
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev, sc, &n, nullptr);
    scImages.resize(n);
    vkGetSwapchainImagesKHR(dev, sc, &n, scImages.data());
    scViews.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = scImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = scFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(dev, &vi, nullptr, &scViews[i]));
    }
    renderSems.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK(vkCreateSemaphore(dev, &s, nullptr, &renderSems[i]));
    }
    bool msOn = samples != VK_SAMPLE_COUNT_1_BIT;
    rext.width = std::max(16u, (uint32_t)(ext.width * renderScale)) & ~1u;
    rext.height = std::max(16u, (uint32_t)(ext.height * renderScale)) & ~1u;
    if (!makeImage(depth, rext.width, rext.height, 1, 1, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples, VK_IMAGE_ASPECT_DEPTH_BIT, false)) return false;
    if (msOn && !makeImage(msaaColor, rext.width, rext.height, 1, 1, scFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, samples, VK_IMAGE_ASPECT_COLOR_BIT, false)) return false;
    for (int i = 0; i < FRAMES; i++)
        if (!makeImage(offImg[i], rext.width, rext.height, 1, 1, scFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT, false)) return false;
    if (!createMainRP()) return false;
    fbs.resize(FRAMES);
    for (int i = 0; i < FRAMES; i++) {
        VkImageView at[3];
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        if (msOn) { at[0] = msaaColor.v; at[1] = depth.v; at[2] = offImg[i].v; fi.attachmentCount = 3; }
        else { at[0] = offImg[i].v; at[1] = depth.v; fi.attachmentCount = 2; }
        fi.renderPass = mainRP; fi.pAttachments = at; fi.width = rext.width; fi.height = rext.height; fi.layers = 1;
        VK(vkCreateFramebuffer(dev, &fi, nullptr, &fbs[i]));
    }
    if (!createPipelines()) return false;
    LOGI("swapchain %ux%u, %u images, fmt %d", ext.width, ext.height, n, (int)scFormat);
    return true;
}

void Renderer::destroySwapchain() {
    if (!dev) return;
    vkDeviceWaitIdle(dev);
    for (auto f : fbs) vkDestroyFramebuffer(dev, f, nullptr);
    fbs.clear();
    for (auto s : renderSems) vkDestroySemaphore(dev, s, nullptr);
    renderSems.clear();
    for (auto v : scViews) vkDestroyImageView(dev, v, nullptr);
    scViews.clear();
    VkPipeline* ps[] = {&pLit, &pWater, &pSky, &pPartA, &pPartAdd};
    for (auto p : ps) if (*p) { vkDestroyPipeline(dev, *p, nullptr); *p = VK_NULL_HANDLE; }
    if (mainRP) { vkDestroyRenderPass(dev, mainRP, nullptr); mainRP = VK_NULL_HANDLE; }
    freeImg(depth); freeImg(msaaColor); for (int i = 0; i < FRAMES; i++) freeImg(offImg[i]);
    if (sc) { vkDestroySwapchainKHR(dev, sc, nullptr); sc = VK_NULL_HANDLE; }
    if (surface) { vkDestroySurfaceKHR(inst, surface, nullptr); surface = VK_NULL_HANDLE; }
    if (window) { ANativeWindow_release(window); window = nullptr; }
    // re-arm fences/semaphores: acquire semaphores may be in a signaled state; recreate
    for (int i = 0; i < FRAMES; i++) {
        vkDestroySemaphore(dev, acquireSems[i], nullptr);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(dev, &si, nullptr, &acquireSems[i]);
        vkResetFences(dev, 1, &fences[i]);
        VkSubmitInfo e{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        vkQueueSubmit(queue, 1, &e, fences[i]);  // empty submit re-signals the fence
    }
    vkQueueWaitIdle(queue);
    ready = false;
}

bool Renderer::setSurface(ANativeWindow* w, int, int) {
    destroySwapchain();
    if (!w) return true;
    window = w;
    ANativeWindow_acquire(window);
    VkAndroidSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    ci.window = window;
    VK(vkCreateAndroidSurfaceKHR(inst, &ci, nullptr, &surface));
    if (!createSwapchain()) return false;
    ready = true;
    return true;
}

void Renderer::shutdown() {
    if (dev) {
        destroySwapchain();
        vkDeviceWaitIdle(dev);
        for (int i = 0; i < FRAMES; i++) { freeBuf(ubo[i]); freeBuf(dbuf[i]); vkDestroyFence(dev, fences[i], nullptr); vkDestroySemaphore(dev, acquireSems[i], nullptr); }
        freeBuf(vbuf); freeBuf(ibuf); freeBuf(sbuf);
        freeImg(albedo); freeImg(normalTex); freeImg(shadow);
        if (texSampler) vkDestroySampler(dev, texSampler, nullptr);
        if (shadowSampler) vkDestroySampler(dev, shadowSampler, nullptr);
        if (pShadow) vkDestroyPipeline(dev, pShadow, nullptr);
        if (shadowFB) vkDestroyFramebuffer(dev, shadowFB, nullptr);
        if (shadowRP) vkDestroyRenderPass(dev, shadowRP, nullptr);
        for (int i = 0; i < nModules; i++) vkDestroyShaderModule(dev, modules[i], nullptr);
        if (dpool) vkDestroyDescriptorPool(dev, dpool, nullptr);
        if (plLayout) vkDestroyPipelineLayout(dev, plLayout, nullptr);
        if (dsl0) vkDestroyDescriptorSetLayout(dev, dsl0, nullptr);
        if (dsl1) vkDestroyDescriptorSetLayout(dev, dsl1, nullptr);
        if (pool) vkDestroyCommandPool(dev, pool, nullptr);
        vkDestroyDevice(dev, nullptr);
        dev = VK_NULL_HANDLE;
    }
    if (inst) { vkDestroyInstance(inst, nullptr); inst = VK_NULL_HANDLE; }
}

// ---------- per frame ----------
void Renderer::draw(const float* params, int n) {
    if (!ready) return;
    float p[P_COUNT] = {0};
    memcpy(p, params, sizeof(float) * std::min(n, (int)P_COUNT));
    int f = frame;
    vkWaitForFences(dev, 1, &fences[f], VK_TRUE, UINT64_MAX);
    uint32_t idx;
    VkResult r = vkAcquireNextImageKHR(dev, sc, UINT64_MAX, acquireSems[f], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            ANativeWindow* w = window; ANativeWindow_acquire(w);
            setSurface(w, 0, 0); ANativeWindow_release(w);
            return;
        }
    } else if (r != VK_SUCCESS) { LOGE("acquire failed %d", (int)r); return; }
    vkResetFences(dev, 1, &fences[f]);

    // camera
    Camera cam;
    float aspect = (float)rext.width / (float)rext.height;
    SceneParams sp; memcpy(sp.p, p, sizeof(p));
    // camera from scene
    {
        float scroll = clampf(p[P_SCROLL], 0.f, 1.f);
        vec3 eye = scene.cameraPos(scroll, p[P_TIME]);
        float yaw = (scroll - 0.5f) * 0.22f;
        float pitch = -0.035f;
        vec3 fwd = norm(vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)));
        vec3 right = norm(cross(vec3(0, 1, 0), fwd));
        vec3 up = cross(fwd, right);
        cam.eye = eye; cam.right = right; cam.up = up; cam.fwd = fwd;
        cam.aspect = aspect;
        cam.fovY = aspect < 1.f ? 1.22f : 0.95f;
    }
    FrameUBO& u = *(FrameUBO*)ubo[f].map;
    fillFrame(u, p, cam);
    for (int m = 0; m < MESH_COUNT; m++) dyn[m].clear();
    scene.update(sp, dyn, u);

    // pack dynamic instances
    struct DD { int mesh; uint32_t first, count; };
    std::vector<DD> dd;
    uint32_t off = 0;
    for (int m = 0; m < MESH_COUNT; m++) {
        if (dyn[m].empty()) continue;
        uint32_t c = (uint32_t)std::min<size_t>(dyn[m].size(), MAX_DYN - off);
        if (!c) break;
        memcpy((Instance*)dbuf[f].map + off, dyn[m].data(), c * sizeof(Instance));
        dd.push_back({m, off, c});
        off += c;
    }

    VkCommandBuffer c = cmds[f];
    vkResetCommandBuffer(c, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(c, &bi);
    VkDescriptorSet sets[2] = {set0[f], set1};
    VkDeviceSize zero = 0;
    auto drawMesh = [&](int mesh, VkBuffer instBuf, uint32_t first, uint32_t count) {
        if (!indexCount[mesh]) return;
        VkBuffer vb[2] = {vbuf.b, instBuf};
        VkDeviceSize offs[2] = {0, (VkDeviceSize)first * sizeof(Instance)};
        vkCmdBindVertexBuffers(c, 0, 2, vb, offs);
        vkCmdBindIndexBuffer(c, ibuf.b, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(c, indexCount[mesh], count, firstIndex[mesh], (int32_t)baseVertex[mesh], 0);
    };
    // 1. shadow pass
    if (u.sunColor[3] > 0.02f && (u.sunDir[3] > 0.02f || u.moonDir[1] > 0.05f)) {
        VkClearValue cv; cv.depthStencil = {1.f, 0};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = shadowRP; rb.framebuffer = shadowFB; rb.renderArea = {{0, 0}, {(uint32_t)shadowSize, (uint32_t)shadowSize}};
        rb.clearValueCount = 1; rb.pClearValues = &cv;
        vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pShadow);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, plLayout, 0, 2, sets, 0, nullptr);
        VkViewport vp{0, 0, (float)shadowSize, (float)shadowSize, 0, 1};
        VkRect2D sc2{{0, 0}, {(uint32_t)shadowSize, (uint32_t)shadowSize}};
        vkCmdSetViewport(c, 0, 1, &vp); vkCmdSetScissor(c, 0, 1, &sc2);
        for (auto& d : staticDraws) if (d.shadowCount) drawMesh(d.mesh, sbuf.b, d.firstInstance, d.shadowCount);
        for (auto& d : dd) if (scene.shadowCaster[d.mesh]) drawMesh(d.mesh, dbuf[f].b, d.first, d.count);
        vkCmdEndRenderPass(c);
    }
    // 2. main pass
    VkClearValue cvs[3];
    cvs[0].color = {{0, 0, 0, 1}}; cvs[1].depthStencil = {0.f, 0}; cvs[2].color = {{0, 0, 0, 1}};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = mainRP; rb.framebuffer = fbs[f]; rb.renderArea = {{0, 0}, rext};
    rb.clearValueCount = 3; rb.pClearValues = cvs;
    vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)rext.width, (float)rext.height, 0, 1};
    VkRect2D scr{{0, 0}, rext};
    vkCmdSetViewport(c, 0, 1, &vp); vkCmdSetScissor(c, 0, 1, &scr);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, plLayout, 0, 2, sets, 0, nullptr);
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pLit);
    // opaque objects first (front-to-back sorted at build time), the heavy terrain shader last so early-z culls hidden pixels
    for (auto& d : staticDraws) if (d.mesh != MESH_WATER && d.mesh != MESH_TERRAIN) drawMesh(d.mesh, sbuf.b, d.firstInstance, d.count);
    for (auto& d : dd) drawMesh(d.mesh, dbuf[f].b, d.first, d.count);
    for (auto& d : staticDraws) if (d.mesh == MESH_TERRAIN) drawMesh(d.mesh, sbuf.b, d.firstInstance, d.count);
    // water
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pWater);
    for (auto& d : staticDraws) if (d.mesh == MESH_WATER) drawMesh(d.mesh, sbuf.b, d.firstInstance, d.count);
    // sky
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pSky);
    vkCmdDraw(c, 3, 1, 0, 0);
    // particles
    auto part = [&](VkPipeline pipe, int type, int count, float a, float b) {
        if (count <= 0) return;
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        float pc[4]; int ti[2] = {type, count}; memcpy(pc, ti, 8); pc[2] = a; pc[3] = b;
        vkCmdPushConstants(c, plLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
        vkCmdDraw(c, 6, count, 0, 0);
    };
    float q = quality == 0 ? 0.5f : (quality == 1 ? 0.8f : 1.f);
    part(pPartA, 2, 3 * 30, 0, 0);
    part(pPartA, 0, (int)(p[P_SNOWFALL] * 5500 * q), 0, 0);
    part(pPartA, 1, (int)(p[P_RAIN] * 4500 * q), 0, 0);
    part(pPartA, 3, (int)(p[P_AUTUMN] * p[P_LEAF] * 260 * q * (1.0f - clampf(p[P_SNOW], 0, 1))), 0, 0);
    part(pPartAdd, 4, 40, 0, 0);
    float ffs = (p[P_SEASON] > 1.5f && p[P_SEASON] < 2.6f) ? 1.f : 0.f;
    float nightF = u.skyAmbient[3];
    part(pPartAdd, 4, (int)(ffs * nightF * (1.f - p[P_RAIN]) * 70), 1.f, scene.heightAt(0, 30) + 0.8f);
    if (p[P_EVENT] >= 1.f && (((int)(p[P_EVENT] + 0.5f)) & (EV_FIREWORKS)) && nightF > 0.4f) part(pPartAdd, 5, 4 * 70, 1.f, 0);
    vkCmdEndRenderPass(c);
    // upscale the reduced-resolution frame into the swapchain image
    {
        VkImageMemoryBarrier b1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b1.image = scImages[idx]; b1.srcQueueFamilyIndex = b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);
        VkImageBlit bl{};
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; bl.srcOffsets[1] = {(int32_t)rext.width, (int32_t)rext.height, 1};
        bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; bl.dstOffsets[1] = {(int32_t)ext.width, (int32_t)ext.height, 1};
        vkCmdBlitImage(c, offImg[f].i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scImages[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b2.dstAccessMask = 0;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);
    }
    vkEndCommandBuffer(c);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &acquireSems[f]; si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1; si.pCommandBuffers = &c; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderSems[idx];
    VKV(vkQueueSubmit(queue, 1, &si, fences[f]));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderSems[idx]; pi.swapchainCount = 1; pi.pSwapchains = &sc; pi.pImageIndices = &idx;
    VkResult pr = vkQueuePresentKHR(queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR) { ANativeWindow* w = window; ANativeWindow_acquire(w); setSurface(w, 0, 0); ANativeWindow_release(w); }
    frame = (frame + 1) % FRAMES;
}

// ---------- C API ----------
Renderer* rendererCreate(AAssetManager* am, int quality) {
    Renderer* r = new Renderer();
    r->am = am; r->quality = quality;
    if (!r->init()) { LOGE("renderer init failed"); r->shutdown(); delete r; return nullptr; }
    return r;
}
void rendererDestroy(Renderer* r) { if (r) { r->shutdown(); delete r; } }
bool rendererSetSurface(Renderer* r, ANativeWindow* win, int w, int h) { return r && r->setSurface(win, w, h); }
void rendererDraw(Renderer* r, const float* params, int n) { if (r) r->draw(params, n); }
bool rendererReady(Renderer* r) { return r && r->ready; }
