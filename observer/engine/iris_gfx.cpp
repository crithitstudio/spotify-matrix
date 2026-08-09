// IRIS engine - image IO, screenshots, 2D overlay queue.
#include "iris_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace iris {

TexId Engine::loadTexture(const std::string& path, bool srgb, bool repeat) {
    int w, h, comp;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels) {
        IRIS_WARN("Texture load failed: %s", path.c_str());
        return TEX_WHITE;
    }
    TexId id = createTexture(pixels, w, h, srgb, repeat, true);
    stbi_image_free(pixels);
    return id;
}

bool Engine::saveScreenshot(const std::string& pngPath) {
    std::vector<uint8_t> pixels;
    if (!impl->readImageRGBA(impl->finalImage, pixels)) return false;
    // alpha channel of the final image is undefined post-blend; force opaque
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    int ok = stbi_write_png(pngPath.c_str(), (int)impl->finalImage.w, (int)impl->finalImage.h, 4,
                            pixels.data(), (int)impl->finalImage.w * 4);
    if (ok) IRIS_INFO("Screenshot: %s", pngPath.c_str());
    return ok != 0;
}

// ------------------------------------------------------------- UI overlay --
static void pushQuad(std::vector<UiVertex>& v, float x, float y, float w, float h, vec2 uv0, vec2 uv1,
                     vec4 col) {
    UiVertex a{{x, y}, {uv0.x, uv0.y}, col};
    UiVertex b{{x + w, y}, {uv1.x, uv0.y}, col};
    UiVertex c{{x + w, y + h}, {uv1.x, uv1.y}, col};
    UiVertex d{{x, y + h}, {uv0.x, uv1.y}, col};
    v.push_back(a); v.push_back(b); v.push_back(c);
    v.push_back(a); v.push_back(c); v.push_back(d);
}

void Engine::uiImage(TexId tex, float x, float y, float w, float h, vec4 color, vec2 uv0, vec2 uv1) {
    Impl& im = *impl;
    uint32_t first = (uint32_t)im.uiVerts.size();
    pushQuad(im.uiVerts, x, y, w, h, uv0, uv1, color);
    if (!im.uiBatches.empty() && im.uiBatches.back().tex == tex &&
        im.uiBatches.back().firstVertex + im.uiBatches.back().vertexCount == first) {
        im.uiBatches.back().vertexCount += 6;
    } else {
        im.uiBatches.push_back({tex, first, 6});
    }
}

void Engine::uiRect(float x, float y, float w, float h, vec4 color) {
    uiImage(TEX_WHITE, x, y, w, h, color);
}

} // namespace iris
