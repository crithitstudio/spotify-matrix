// IRIS engine - stb_truetype font baking and text drawing.
#include "iris_internal.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

namespace iris {

static constexpr int ATLAS_W = 1024;
static constexpr int ATLAS_H = 1024;
static constexpr int FIRST_CHAR = 32;
static constexpr int NUM_CHARS = 224;   // latin-1 coverage

struct FontBake {
    stbtt_packedchar chars[NUM_CHARS];
};

Font* Engine::loadFont(const std::string& ttfPath, float pixelHeight) {
    Font* font = new Font();
    font->pixelHeight = pixelHeight;
    if (!readFileBytes(ttfPath, font->ttf)) {
        IRIS_ERROR("Font not found: %s", ttfPath.c_str());
        delete font;
        return nullptr;
    }

    std::vector<uint8_t> atlas((size_t)ATLAS_W * ATLAS_H);
    stbtt_pack_context pack;
    stbtt_PackBegin(&pack, atlas.data(), ATLAS_W, ATLAS_H, 0, 1, nullptr);
    stbtt_PackSetOversampling(&pack, 2, 2);
    FontBake bake;
    stbtt_PackFontRange(&pack, font->ttf.data(), 0, pixelHeight, FIRST_CHAR, NUM_CHARS, bake.chars);
    // general punctuation (en/em dash, quotes, ellipsis, bullet)
    constexpr int PUNCT_FIRST = 0x2010, PUNCT_COUNT = 0x2030 - 0x2010;
    stbtt_packedchar punct[PUNCT_COUNT];
    stbtt_PackFontRange(&pack, font->ttf.data(), 0, pixelHeight, PUNCT_FIRST, PUNCT_COUNT, punct);
    stbtt_PackEnd(&pack);

    // coverage -> white RGBA
    std::vector<uint8_t> rgba((size_t)ATLAS_W * ATLAS_H * 4, 255);
    for (size_t i = 0; i < atlas.size(); ++i) rgba[i * 4 + 3] = atlas[i];
    font->atlas = createTexture(rgba.data(), ATLAS_W, ATLAS_H, false, false, true);

    stbtt_fontinfo* info = new stbtt_fontinfo();
    stbtt_InitFont(info, font->ttf.data(), stbtt_GetFontOffsetForIndex(font->ttf.data(), 0));
    font->stbInfo = info;
    float scale = stbtt_ScaleForPixelHeight(info, pixelHeight);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    font->ascent = asc * scale;
    font->descent = desc * scale;
    font->lineGap = gap * scale;

    auto storeGlyph = [&](uint32_t cp, const stbtt_packedchar& pc) {
        Glyph g;
        g.x0 = pc.x0 / (float)ATLAS_W;
        g.y0 = pc.y0 / (float)ATLAS_H;
        g.x1 = pc.x1 / (float)ATLAS_W;
        g.y1 = pc.y1 / (float)ATLAS_H;
        g.xoff = pc.xoff;
        g.yoff = pc.yoff;
        g.xadv = pc.xadvance;
        g.w = (float)(pc.x1 - pc.x0) / 2.0f;   // oversampled 2x
        g.h = (float)(pc.y1 - pc.y0) / 2.0f;
        font->glyphs[cp] = g;
    };
    for (int i = 0; i < NUM_CHARS; ++i) storeGlyph((uint32_t)(FIRST_CHAR + i), bake.chars[i]);
    for (int i = 0; i < PUNCT_COUNT; ++i) storeGlyph((uint32_t)(PUNCT_FIRST + i), punct[i]);
    impl->fonts.push_back(font);
    return font;
}

// Minimal UTF-8 decode; unknown sequences map to '?'.
static uint32_t nextCodepoint(const std::string& s, size_t& i) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) { i += 1; return c; }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) {
        uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    if ((c >> 4) == 0xE && i + 2 < s.size()) {
        uint32_t cp = ((c & 0x0F) << 12) | (((uint8_t)s[i + 1] & 0x3F) << 6) | ((uint8_t)s[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    i += 1;
    return '?';
}

float Engine::uiText(Font* font, const std::string& utf8, float x, float y, vec4 color, float scale) {
    if (!font) return 0;
    float penX = x;
    float baseline = y + font->ascent * scale;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (cp == '\n') {
            penX = x;
            baseline += (font->ascent - font->descent + font->lineGap) * scale;
            continue;
        }
        auto it = font->glyphs.find(cp);
        if (it == font->glyphs.end()) it = font->glyphs.find('?');
        if (it == font->glyphs.end()) continue;
        const Glyph& g = it->second;
        float gx = penX + g.xoff * scale;
        float gy = baseline + g.yoff * scale;
        if (g.w > 0 && g.h > 0)
            uiImage(font->atlas, gx, gy, g.w * scale, g.h * scale, color, {g.x0, g.y0}, {g.x1, g.y1});
        penX += g.xadv * scale;
    }
    return penX - x;
}

float Engine::textWidth(Font* font, const std::string& utf8, float scale) const {
    if (!font) return 0;
    float w = 0, line = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (cp == '\n') { w = std::fmax(w, line); line = 0; continue; }
        auto it = font->glyphs.find(cp);
        if (it == font->glyphs.end()) it = font->glyphs.find('?');
        if (it != font->glyphs.end()) line += it->second.xadv * scale;
    }
    return std::fmax(w, line);
}

float Engine::fontHeight(Font* font) const {
    return font ? (font->ascent - font->descent) : 0;
}

} // namespace iris
