// IRIS engine - platform layer. GLFW window when available, headless otherwise.
#include "iris_internal.h"

#include <chrono>

#if IRIS_WITH_GLFW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace iris {

double platformTime() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

#if IRIS_WITH_GLFW

static float g_wheel = 0;
static bool g_prevDown[KEY_COUNT] = {};
static bool g_prevMouse[3] = {};
static double g_lastX = 0, g_lastY = 0;
static bool g_haveCursor = false;

static int mapKey(Key k) {
    switch (k) {
        case KEY_W: return GLFW_KEY_W;
        case KEY_A: return GLFW_KEY_A;
        case KEY_S: return GLFW_KEY_S;
        case KEY_D: return GLFW_KEY_D;
        case KEY_E: return GLFW_KEY_E;
        case KEY_F: return GLFW_KEY_F;
        case KEY_Q: return GLFW_KEY_Q;
        case KEY_R: return GLFW_KEY_R;
        case KEY_T: return GLFW_KEY_T;
        case KEY_C: return GLFW_KEY_C;
        case KEY_M: return GLFW_KEY_M;
        case KEY_P: return GLFW_KEY_P;
        case KEY_TAB: return GLFW_KEY_TAB;
        case KEY_SPACE: return GLFW_KEY_SPACE;
        case KEY_SHIFT: return GLFW_KEY_LEFT_SHIFT;
        case KEY_CTRL: return GLFW_KEY_LEFT_CONTROL;
        case KEY_ESC: return GLFW_KEY_ESCAPE;
        case KEY_ENTER: return GLFW_KEY_ENTER;
        case KEY_BACKSPACE: return GLFW_KEY_BACKSPACE;
        case KEY_UP: return GLFW_KEY_UP;
        case KEY_DOWN: return GLFW_KEY_DOWN;
        case KEY_LEFT: return GLFW_KEY_LEFT;
        case KEY_RIGHT: return GLFW_KEY_RIGHT;
        case KEY_1: return GLFW_KEY_1;
        case KEY_2: return GLFW_KEY_2;
        case KEY_3: return GLFW_KEY_3;
        case KEY_4: return GLFW_KEY_4;
        case KEY_5: return GLFW_KEY_5;
        case KEY_6: return GLFW_KEY_6;
        case KEY_7: return GLFW_KEY_7;
        case KEY_8: return GLFW_KEY_8;
        case KEY_9: return GLFW_KEY_9;
        case KEY_0: return GLFW_KEY_0;
        case KEY_F5: return GLFW_KEY_F5;
        case KEY_F9: return GLFW_KEY_F9;
        case KEY_F12: return GLFW_KEY_F12;
        default: return GLFW_KEY_UNKNOWN;
    }
}

bool platformInit(Engine::Impl& impl) {
    if (!glfwInit()) {
        IRIS_ERROR("glfwInit failed");
        return false;
    }
    if (!glfwVulkanSupported()) {
        IRIS_ERROR("GLFW reports no Vulkan support (missing loader?)");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWmonitor* mon = impl.cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    uint32_t w = impl.cfg.width, h = impl.cfg.height;
    if (mon) {
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        w = vm->width;
        h = vm->height;
    }
    impl.window = glfwCreateWindow((int)w, (int)h, impl.cfg.appName.c_str(), mon, nullptr);
    if (!impl.window) {
        IRIS_ERROR("glfwCreateWindow failed");
        return false;
    }
    glfwSetScrollCallback(impl.window, [](GLFWwindow*, double, double dy) { g_wheel += (float)dy; });
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(impl.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    return true;
}

void platformShutdown(Engine::Impl& impl) {
    if (impl.window) {
        glfwDestroyWindow(impl.window);
        impl.window = nullptr;
        glfwTerminate();
    }
}

std::vector<const char*> platformInstanceExtensions(Engine::Impl&) {
    uint32_t count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(exts, exts + count);
}

bool platformCreateSurface(Engine::Impl& impl) {
    return glfwCreateWindowSurface(impl.instance, impl.window, nullptr, &impl.surface) == VK_SUCCESS;
}

void platformGetFramebufferSize(Engine::Impl& impl, uint32_t& w, uint32_t& h) {
    int iw = 0, ih = 0;
    glfwGetFramebufferSize(impl.window, &iw, &ih);
    w = (uint32_t)iw;
    h = (uint32_t)ih;
}

void platformSetMouseCaptured(Engine::Impl& impl, bool captured) {
    glfwSetInputMode(impl.window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    g_haveCursor = false;
}

bool platformPump(Engine& engine, Engine::Impl& impl) {
    glfwPollEvents();
    if (glfwWindowShouldClose(impl.window)) return false;

    Input& in = engine.input();
    for (int k = 0; k < KEY_COUNT; ++k) {
        int gk = mapKey((Key)k);
        bool down = gk != GLFW_KEY_UNKNOWN && glfwGetKey(impl.window, gk) == GLFW_PRESS;
        in.down[k] = down;
        in.pressed[k] = down && !g_prevDown[k];
        g_prevDown[k] = down;
    }
    for (int b = 0; b < 3; ++b) {
        bool down = glfwGetMouseButton(impl.window, b) == GLFW_PRESS;
        in.mouseDown[b] = down;
        in.mousePressed[b] = down && !g_prevMouse[b];
        g_prevMouse[b] = down;
    }
    double cx, cy;
    glfwGetCursorPos(impl.window, &cx, &cy);
    if (g_haveCursor) {
        in.mouseDX = (float)(cx - g_lastX);
        in.mouseDY = (float)(cy - g_lastY);
    }
    g_lastX = cx;
    g_lastY = cy;
    g_haveCursor = true;
    in.wheel = g_wheel;
    g_wheel = 0;

    // resize detection
    uint32_t fw, fh;
    platformGetFramebufferSize(impl, fw, fh);
    if (impl.swapchain && (fw != impl.swapExtent.width || fh != impl.swapExtent.height) && fw && fh)
        impl.swapchainDirty = true;
    return true;
}

#else   // ---- headless-only build

bool platformInit(Engine::Impl&) {
    IRIS_ERROR("Built without GLFW: only --headless mode is available");
    return false;
}
void platformShutdown(Engine::Impl&) {}
bool platformPump(Engine&, Engine::Impl&) { return true; }
void platformSetMouseCaptured(Engine::Impl&, bool) {}
bool platformCreateSurface(Engine::Impl&) { return false; }
std::vector<const char*> platformInstanceExtensions(Engine::Impl&) { return {}; }
void platformGetFramebufferSize(Engine::Impl& impl, uint32_t& w, uint32_t& h) {
    w = impl.cfg.width;
    h = impl.cfg.height;
}

#endif

} // namespace iris
