// IRIS - a small custom Vulkan 1.3 engine built for THE OBSERVER.
// "It Renders In Silence."
//
// Design: one fixed pipeline set (scene / ui / post), bindless-lite texture
// array, dynamic rendering only (no VkRenderPass), optional GLFW window or
// fully headless offscreen operation (CI, screenshot tests, CPU Vulkan).
#pragma once
#include "iris_core.h"
#include "iris_math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace iris {

// ----------------------------------------------------------------- config --
struct EngineConfig {
    std::string appName = "IRIS";
    uint32_t width = 1600, height = 900;
    bool headless = false;      // no window, render offscreen only
    bool fullscreen = false;
    bool vsync = true;
    bool validation = false;
    std::string shaderDir = "shaders/spv";  // compiled SPIR-V location
};

// ------------------------------------------------------------------ input --
enum Key {
    KEY_W, KEY_A, KEY_S, KEY_D, KEY_E, KEY_F, KEY_Q, KEY_R, KEY_T, KEY_C, KEY_M, KEY_P,
    KEY_TAB, KEY_SPACE, KEY_SHIFT, KEY_CTRL, KEY_ESC, KEY_ENTER, KEY_BACKSPACE,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_F5, KEY_F9, KEY_F12,
    KEY_COUNT
};

struct Input {
    bool down[KEY_COUNT] = {};
    bool pressed[KEY_COUNT] = {};   // edge, cleared each frame
    bool mouseDown[3] = {};
    bool mousePressed[3] = {};
    float mouseDX = 0, mouseDY = 0;
    float wheel = 0;
    // Scripted input injection (headless sim / demo playback).
    void injectKey(Key k, bool isDown) {
        if (isDown && !down[k]) pressed[k] = true;
        down[k] = isDown;
    }
};

// --------------------------------------------------------------- graphics --
using TexId = int32_t;
using MeshId = int32_t;
using RtId = int32_t;
constexpr TexId TEX_INVALID = -1;
constexpr TexId TEX_WHITE = 0;   // engine guarantees slot 0 is 1x1 white

struct Vertex {
    vec3 pos;
    vec3 nrm;
    vec2 uv;
    vec4 col{1, 1, 1, 1};   // rgb = tint/baked AO, a = unused
};

enum DrawFlags : uint32_t {
    DRAW_UNLIT       = 1u << 0,  // skip lighting, texture*tint as-is
    DRAW_ALPHA       = 1u << 1,  // alpha blend, drawn back-to-front after opaque
    DRAW_NO_REFLECT  = 1u << 2,  // omit from reflection views
    DRAW_ONLY_REFLECT= 1u << 3,  // draw only in reflection views
    DRAW_NO_MAIN     = 1u << 4,  // omit from main view (e.g. CCTV-only apparitions)
    DRAW_DOUBLE_SIDED= 1u << 5,
    DRAW_ALPHA_TEST  = 1u << 6,  // discard texel alpha < 0.5 (foliage-style cutout)
};

struct DrawItem {
    MeshId mesh = -1;
    TexId tex = TEX_WHITE;
    mat4 model;
    vec4 tint{1, 1, 1, 1};
    float emissive = 0.0f;      // 0 lit, >0 adds texture*emissive
    float uvScaleX = 1.0f, uvScaleY = 1.0f;
    uint32_t flags = 0;
};

enum LightType : int32_t { LIGHT_POINT = 0, LIGHT_SPOT = 1 };

struct Light {
    vec3 pos;
    float radius = 6.0f;        // attenuation range
    vec3 color{1, 1, 1};
    float intensity = 1.0f;
    vec3 dir{0, -1, 0};         // spot only
    float innerCos = 0.9f, outerCos = 0.75f;
    int32_t type = LIGHT_POINT;
};

struct Camera {
    vec3 pos{0, 1.7f, 0};
    float yaw = 0;              // radians, 0 = -Z
    float pitch = 0;
    float fovY = radians(70.0f);
    float zNear = 0.05f, zFar = 120.0f;
};

// Explicit basis helpers so trig sign conventions live in exactly one place.
inline vec3 camForward(float yaw, float pitch) {
    return normalize(vec3{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
}
inline vec3 camRight(float yaw) { return {std::cos(yaw), 0, -std::sin(yaw)}; }
inline mat4 camView(vec3 pos, float yaw, float pitch) {
    vec3 f = camForward(yaw, pitch);
    return lookAt(pos, pos + f, {0, 1, 0});
}

struct FogParams {
    vec3 color{0.012f, 0.012f, 0.016f};
    float density = 0.10f;      // exp fog
};

struct SceneView {
    mat4 view;                  // world -> view
    mat4 proj;                  // view -> clip (Vulkan conventions)
    vec3 camPos;
    float time = 0;
    vec3 ambient{0.05f, 0.05f, 0.06f};
    FogParams fog;
    RtId target = -1;           // -1 = main offscreen scene buffer
    bool isReflection = false;  // flips winding, applies clip plane
    vec4 clipPlane{0, 0, 0, 0}; // world-space plane for reflections (n.xyz, d)
    uint32_t layerVisible = 0xffffffffu;  // tested against DrawItem flags NO_MAIN/ONLY_REFLECT
};

struct PostParams {
    float grain = 0.06f;
    float vignette = 0.35f;
    float aberration = 0.0015f;
    float scanline = 0.0f;      // CRT/CCTV scanlines 0..1
    float noiseBurst = 0.0f;    // full-frame static 0..1
    float desaturate = 0.0f;
    float warp = 0.0f;          // fear-driven radial wobble
    float fadeBlack = 0.0f;     // 0 none, 1 fully black
    float flashWhite = 0.0f;
    float cctv = 0.0f;          // 1 = full CCTV look (mono green, timestamp bar)
    float time = 0.0f;
    float lowHealth = 0.0f;     // unused meter-free pulse (kept subtle)
};

// 2D overlay vertex (pixels, top-left origin).
struct UiVertex {
    vec2 pos;
    vec2 uv;
    vec4 col;
};

// ------------------------------------------------------------------ audio --
using SoundId = int32_t;
using VoiceId = int32_t;

class AudioEngine {
public:
    bool init(bool disabled = false);
    void shutdown();
    SoundId load(const std::string& path);           // wav/mp3/flac
    // spatial: false = 2D UI/music bed
    VoiceId play(SoundId snd, float volume = 1.0f, float pitch = 1.0f, bool loop = false,
                 bool spatial = false, vec3 pos = {});
    void stop(VoiceId v);
    void stopAll();
    void setVoiceVolume(VoiceId v, float vol);
    void setVoicePitch(VoiceId v, float p);
    void setVoicePos(VoiceId v, vec3 pos);
    bool isPlaying(VoiceId v);
    void setListener(vec3 pos, vec3 fwd);
    void setMasterVolume(float v);
    struct Impl;
    std::unique_ptr<Impl> impl;
    AudioEngine();
    ~AudioEngine();
};

// ------------------------------------------------------------------- text --
struct Font;   // opaque, created by Engine::loadFont

// ----------------------------------------------------------------- engine --
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;

    bool init(const EngineConfig& cfg);
    void shutdown();

    // Window/event pump. Returns false when the app should quit.
    bool pumpEvents();
    Input& input() { return input_; }
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const;
    double timeSeconds() const;
    uint32_t width() const;
    uint32_t height() const;
    bool headless() const;
    void requestQuit();

    // ---- resources
    TexId createTexture(const void* rgba8, int w, int h, bool srgb = true, bool repeat = true,
                        bool filter = true);
    TexId loadTexture(const std::string& path, bool srgb = true, bool repeat = true);
    void updateTexture(TexId id, const void* rgba8, int w, int h);  // same size re-upload
    MeshId createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    RtId createRenderTarget(uint32_t w, uint32_t h);
    TexId renderTargetTexture(RtId rt) const;

    Font* loadFont(const std::string& ttfPath, float pixelHeight);

    // ---- frame
    bool beginFrame();   // false if swapchain unavailable this frame (minimized)
    // Render one view of the world. Call once per view (CCTV RTs, mirror RTs,
    // then the main view with target = -1) inside a frame.
    void renderScene(const SceneView& view, const std::vector<DrawItem>& items,
                     const std::vector<Light>& lights);
    // Post-process main scene buffer to the presented image; UI is drawn on top.
    void renderPostAndUi(const PostParams& post);
    void endFrame();

    // ---- 2D overlay (queued, drawn by renderPostAndUi)
    void uiRect(float x, float y, float w, float h, vec4 color);
    void uiImage(TexId tex, float x, float y, float w, float h, vec4 color = {1, 1, 1, 1},
                 vec2 uv0 = {0, 0}, vec2 uv1 = {1, 1});
    // Returns text width in pixels. y is the baseline top (we draw from top-left).
    float uiText(Font* font, const std::string& utf8, float x, float y, vec4 color, float scale = 1.0f);
    float textWidth(Font* font, const std::string& utf8, float scale = 1.0f) const;
    float fontHeight(Font* font) const;

    // ---- readback
    // Save what was presented last endFrame (or current offscreen in headless).
    bool saveScreenshot(const std::string& pngPath);
    // Grab scene buffer into CPU RGBA8 (used by in-game photography).
    bool captureSceneRGBA(std::vector<uint8_t>& out, int& w, int& h);

    struct Impl;
    std::unique_ptr<Impl> impl;

private:
    Input input_;
};

} // namespace iris
