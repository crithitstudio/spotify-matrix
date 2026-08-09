// IRIS internals shared across engine translation units. Not for game code.
#pragma once
#include "iris.h"

#include <volk/volk.h>

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

#if IRIS_WITH_GLFW
struct GLFWwindow;
#endif

namespace iris {

constexpr uint32_t IRIS_MAX_TEXTURES = 512;
constexpr uint32_t IRIS_MAX_LIGHTS = 24;
constexpr uint32_t IRIS_MAX_VIEWS_PER_FRAME = 16;
constexpr uint32_t IRIS_FRAMES_IN_FLIGHT = 2;

#define VK_CHK(call)                                                          \
    do {                                                                      \
        VkResult res_ = (call);                                               \
        if (res_ != VK_SUCCESS) IRIS_FATAL("Vulkan error %d at %s:%d", (int)res_, __FILE__, __LINE__); \
    } while (0)

struct GpuBuffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct GpuImage {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct MeshRes {
    GpuBuffer vbuf, ibuf;
    uint32_t indexCount = 0;
    AABB bounds;
};

struct RenderTargetRes {
    GpuImage color, depth;
    TexId texId = TEX_INVALID;   // color view registered in the bindless array
    uint32_t w = 0, h = 0;
};

// std140 mirror of shaders/scene.* view UBO.
struct GpuLight {
    float posRadius[4];
    float colorIntensity[4];
    float dirInner[4];
    float params[4];   // x=outerCos y=type
};
struct ViewUbo {
    float view[16];
    float proj[16];
    float camPosTime[4];
    float ambientFog[4];    // rgb ambient, w fog density
    float fogColor[4];
    float clipPlane[4];
    int32_t counts[4];      // x=lightCount y=isReflection
    GpuLight lights[IRIS_MAX_LIGHTS];
};

struct FrameData {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    GpuBuffer viewUbos;          // IRIS_MAX_VIEWS_PER_FRAME slots, host visible
    GpuBuffer uiVbuf;            // host visible, grown on demand
    size_t uiVbufCapacity = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

struct Glyph {
    float x0, y0, x1, y1;       // atlas uv
    float xoff, yoff, xadv, w, h;
};

struct Font {
    TexId atlas = TEX_INVALID;
    float pixelHeight = 24;
    float ascent = 0, descent = 0, lineGap = 0;
    std::unordered_map<uint32_t, Glyph> glyphs;   // codepoint -> glyph (baked laz*)
    std::vector<uint8_t> ttf;                     // kept for kerning/rebake
    void* stbInfo = nullptr;                      // stbtt_fontinfo*
};

struct UiBatch {
    TexId tex;
    uint32_t firstVertex, vertexCount;
};

struct Engine::Impl {
    EngineConfig cfg;

    // platform
#if IRIS_WITH_GLFW
    GLFWwindow* window = nullptr;
#endif
    bool mouseCaptured = false;
    bool quitRequested = false;
    double startTime = 0;

    // core vk
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkDeviceSize uboAlign = 256;

    // presentation
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapExtent{};
    std::vector<VkImage> swapImages;
    uint32_t swapImageIndex = 0;
    bool swapchainDirty = false;

    // render images (window-size)
    GpuImage sceneColor;   // lit scene, sampled by post + photo capture
    GpuImage sceneDepth;
    GpuImage finalImage;   // post output; blitted to swapchain / saved headless
    TexId sceneColorTexId = TEX_INVALID;
    uint32_t rw = 0, rh = 0;   // render resolution

    // pipelines
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeOpaque = VK_NULL_HANDLE;
    VkPipeline pipeAlpha = VK_NULL_HANDLE;
    VkPipeline pipeUi = VK_NULL_HANDLE;
    VkPipeline pipePost = VK_NULL_HANDLE;

    // resources
    std::vector<GpuImage> textures;          // indexed by TexId
    std::vector<VkSampler> textureSamplers;  // parallel to textures
    std::array<VkSampler, 4> samplers{};     // repeat/clamp x linear/nearest
    std::vector<MeshRes> meshes;
    std::vector<RenderTargetRes> targets;
    std::vector<Font*> fonts;

    // per-frame
    std::array<FrameData, IRIS_FRAMES_IN_FLIGHT> frames;
    uint32_t frameIndex = 0;      // 0..FRAMES_IN_FLIGHT-1
    uint64_t frameCounter = 0;
    uint32_t viewCursor = 0;      // UBO slots used this frame
    bool inFrame = false;

    VkCommandPool cmdPool = VK_NULL_HANDLE;

    // ui accumulation (CPU side, uploaded in renderPostAndUi)
    std::vector<UiVertex> uiVerts;
    std::vector<UiBatch> uiBatches;

    // ---- helpers implemented in iris_vk.cpp
    bool initVulkan();
    void destroyVulkan();
    bool createSwapchain();
    void destroySwapchain();
    bool createRenderImages(uint32_t w, uint32_t h);
    void destroyRenderImages();
    bool createPipelines();
    VkShaderModule loadShader(const std::string& name);

    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags flags);
    GpuBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags,
                           bool map = false);
    void destroyBuffer(GpuBuffer& b);
    GpuImage createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                         VkImageAspectFlags aspect);
    void destroyImage(GpuImage& img);
    VkCommandBuffer beginOneTime();
    void endOneTime(VkCommandBuffer cmd);
    void barrier(VkCommandBuffer cmd, GpuImage& img, VkImageLayout newLayout,
                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                 VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
    void uploadToImage(GpuImage& img, const void* rgba8);
    TexId registerTexture(GpuImage img, VkSampler sampler);
    void writeTextureDescriptor(TexId id);

    void drawItems(VkCommandBuffer cmd, const SceneView& view, const std::vector<DrawItem>& items,
                   uint32_t uboOffset);
    bool readImageRGBA(GpuImage& img, std::vector<uint8_t>& out);
};

// iris_platform.cpp
bool platformInit(Engine::Impl& impl);
void platformShutdown(Engine::Impl& impl);
bool platformPump(Engine& engine, Engine::Impl& impl);   // false = quit
void platformSetMouseCaptured(Engine::Impl& impl, bool captured);
bool platformCreateSurface(Engine::Impl& impl);
std::vector<const char*> platformInstanceExtensions(Engine::Impl& impl);
double platformTime();
void platformGetFramebufferSize(Engine::Impl& impl, uint32_t& w, uint32_t& h);

} // namespace iris
