#include "gfx.h"
#include "assets.h"
#include "platform.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace gfx {
namespace {

const char *kVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aCol;
layout(location = 3) in float aMode;
layout(location = 4) in vec4 aParams;
layout(location = 5) in vec4 aExtra;

uniform vec2 uScreen;

out vec2 vUV;
out vec4 vCol;
out float vMode;
out vec4 vParams;
out vec4 vExtra;
out vec2 vScreenUV;

void main() {
    vUV = aUV;
    vCol = aCol;
    vMode = aMode;
    vParams = aParams;
    vExtra = aExtra;
    vec2 ndc = (aPos / uScreen) * 2.0 - 1.0;
    vScreenUV = vec2(aPos.x / uScreen.x, 1.0 - aPos.y / uScreen.y);
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

const char *kFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
in vec4 vCol;
in float vMode;
in vec4 vParams;
in vec4 vExtra;
in vec2 vScreenUV;

uniform sampler2D uTex;
uniform sampler2D uBlurTex;
out vec4 oColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float dither(vec2 coord) {
    return (fract(sin(dot(coord, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
}

void main() {
    int mode = int(vMode + 0.5);
    if (mode == 0) {
        oColor = vec4(vCol.rgb + dither(gl_FragCoord.xy), vCol.a);
        return;
    }
    if (mode == 1) {
        vec2 p = (vUV - 0.5) * (vParams.xy * 2.0);
        float d = sdRoundedBox(p, vParams.xy - vec2(0.5), vParams.z);
        float mask = 1.0 - smoothstep(-0.65, 0.65, d);
        if (mask <= 0.001) discard;
        vec4 tex = texture(uTex, vUV) * vCol;
        float gloss = smoothstep(0.45, 0.0, vUV.y) * vParams.w;
        tex.rgb = mix(tex.rgb, vec3(1.0), clamp(gloss, 0.0, 0.25));
        oColor = vec4(tex.rgb, tex.a * mask);
        return;
    }
    if (mode == 2) {
        float a = texture(uTex, vUV).r;
        oColor = vec4(vCol.rgb, vCol.a * a);
        return;
    }
    if (mode == 3) {
        vec2 p = (vUV - 0.5) * (vParams.xy * 2.0);
        float d = sdRoundedBox(p, vParams.xy - vec2(0.5), vParams.z);
        float alpha = (vParams.w <= 0.0)
            ? (1.0 - smoothstep(-0.65, 0.65, d))
            : (1.0 - smoothstep(-0.65, 0.65, abs(d + vParams.w * 0.5) - vParams.w * 0.5));
        if (alpha <= 0.001) discard;
        oColor = vec4(vCol.rgb, vCol.a * alpha);
        return;
    }
    if (mode == 4) {
        vec2 q = (vUV - 0.5) * 2.0;
        float r2 = dot(q, q);
        if (r2 >= 1.0) discard;
        float g = exp(-r2 * 3.2) * (1.0 - r2);
        float a = clamp(g * vCol.a + dither(gl_FragCoord.xy), 0.0, 1.0);
        oColor = vec4(vCol.rgb, a);
        return;
    }
    if (mode == 5) {
        // REAL-TIME BACKDROP-BLURRED LIQUID GLASS (Yoru / macOS 26 style!)
        // vParams = (halfW, halfH, radius, activeGlow 0..1)
        // vCol = glass body tint, vExtra = specular/active rim tint
        vec2 p = (vUV - 0.5) * (vParams.xy * 2.0);
        float d = sdRoundedBox(p, vParams.xy - vec2(0.5), vParams.z);
        float mask = 1.0 - smoothstep(-0.7, 0.7, d);
        if (mask <= 0.001) discard;

        // Subtle lens refraction offset near the curved edges of the glass!
        vec2 edgeDir = normalize(p + vec2(0.0001));
        float edgeBand = smoothstep(-18.0, 0.0, d);
        vec2 refractUV = clamp(vScreenUV - edgeDir * edgeBand * 0.0045, vec2(0.002), vec2(0.998));

        // Sample true blurred backdrop + slightly lift saturation/vibrancy like Apple glass
        vec3 bg = texture(uBlurTex, refractUV).rgb;
        float luma = dot(bg, vec3(0.299, 0.587, 0.114));
        bg = mix(vec3(luma), bg, 1.28);

        // Blend translucent glass tint cleanly over real blurred background
        vec3 glass = mix(bg, vCol.rgb, vCol.a);

        // Soft top-down caustic sheen inside the glass (very gentle, never plastic!)
        float sheen = smoothstep(0.65, 0.0, vUV.y) * (0.065 + vParams.w * 0.05);
        glass += vec3(sheen) * mix(vec3(1.0), vExtra.rgb, 0.45);

        // Inner velvet edge glow when active (just like Yoru's active rose pill in yoru-03.jpg!)
        float innerGlow = exp(d * 0.14) * vParams.w * 0.34;
        glass += vExtra.rgb * innerGlow;

        // Ultra-delicate 1px specular hairline only (brighter on top curve, soft elsewhere - NO heavy outlines!)
        float rim = 1.0 - smoothstep(0.0, 1.25, abs(d + 0.65));
        float topBias = mix(0.28, 1.0, smoothstep(0.85, 0.0, vUV.y));
        float rimStrength = vExtra.a * topBias + vParams.w * 0.55;
        glass = mix(glass, mix(vec3(1.0), vExtra.rgb, 0.55), clamp(rim * rimStrength, 0.0, 0.85));

        oColor = vec4(clamp(glass + dither(gl_FragCoord.xy), 0.0, 1.0), mask);
        return;
    }
    if (mode == 6) {
        vec2 totalHalf = vParams.xy + vec2(vParams.z);
        vec2 p = (vUV - 0.5) * (totalHalf * 2.0);
        float d = sdRoundedBox(p, vParams.xy, vParams.w);
        float alpha = 1.0 - smoothstep(-vParams.z * 0.18, vParams.z, d);
        alpha = alpha * alpha;
        if (alpha <= 0.001) discard;
        oColor = vec4(vCol.rgb, vCol.a * alpha);
        return;
    }
    if (mode == 7) {
        float rad = vParams.x;
        vec2 p = (vUV - 0.5) * ((rad + 2.0) * 2.0);
        float dist = length(p) - rad;
        float alpha = (vParams.y <= 0.0)
            ? (1.0 - smoothstep(-0.65, 0.65, dist))
            : (1.0 - smoothstep(-0.65, 0.65, abs(dist + vParams.y * 0.5) - vParams.y * 0.5));
        if (alpha <= 0.001) discard;
        oColor = vec4(vCol.rgb, vCol.a * alpha);
        return;
    }
    if (mode == 8) {
        float len = vParams.x;
        float r = vParams.y;
        float pad = r + 1.5;
        vec2 p = (vUV - 0.5) * vec2(len + pad * 2.0, pad * 2.0);
        float ax = max(0.0, abs(p.x) - len * 0.5);
        float d = length(vec2(ax, p.y)) - r;
        float alpha = 1.0 - smoothstep(-0.65, 0.65, d);
        if (alpha <= 0.001) discard;
        oColor = vec4(vCol.rgb, vCol.a * alpha);
        return;
    }
    oColor = vCol;
}
)";

// Kawase blur & fullscreen blit shader for real-time backdrop Liquid Glass
const char *kPostVS = R"(#version 300 es
precision highp float;
out vec2 vUV;
void main() {
    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = (pos + 1.0) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char *kPostFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uOffset;
out vec4 oColor;
void main() {
    if (uOffset <= 0.01) {
        oColor = texture(uTex, vUV);
        return;
    }
    vec2 o = uTexel * uOffset;
    vec4 s = texture(uTex, vUV) * 0.20;
    s += texture(uTex, vUV + vec2( o.x,  o.y)) * 0.14;
    s += texture(uTex, vUV + vec2(-o.x,  o.y)) * 0.14;
    s += texture(uTex, vUV + vec2( o.x, -o.y)) * 0.14;
    s += texture(uTex, vUV + vec2(-o.x, -o.y)) * 0.14;
    s += texture(uTex, vUV + vec2( o.x * 1.8, 0.0)) * 0.06;
    s += texture(uTex, vUV + vec2(-o.x * 1.8, 0.0)) * 0.06;
    s += texture(uTex, vUV + vec2(0.0,  o.y * 1.8)) * 0.06;
    s += texture(uTex, vUV + vec2(0.0, -o.y * 1.8)) * 0.06;
    oColor = vec4(s.rgb, 1.0);
}
)";

GLuint compileShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

bool makeFbo(int w, int h, GLuint &fbo, GLuint &tex) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool ok = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

// ============================================================================
//  TrueType Font Atlas Cache
// ============================================================================
struct GlyphInfo {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    float xadvance;
};

struct FontAtlas {
    GLuint tex = 0;
    int dim = 1024;
    int shelfX = 2;
    int shelfY = 2;
    int shelfH = 0;
    float pxSize = 18.0f;
    float ascent = 14.0f;
    float capHeight = 12.0f;
    float scale = 1.0f;
    stbtt_fontinfo info{};
    std::unordered_map<uint32_t, GlyphInfo> glyphs;
};

stbtt_fontinfo g_fonts[4]{};
stbtt_fontinfo g_fallbackFont{};
bool g_hasFallbackFont = false;
std::unordered_map<uint32_t, FontAtlas> g_atlases;

const uint8_t *fontBytes(Font f) {
    switch (f) {
        case Font::Serif: return assets::serifData();
        case Font::SerifItalic: return assets::serifItalicData();
        case Font::Sans: return assets::sansData();
        case Font::Semi: return assets::semiData();
    }
    return assets::sansData();
}

void initFonts() {
    for (int i = 0; i < 4; ++i) stbtt_InitFont(&g_fonts[i], fontBytes(static_cast<Font>(i)), 0);
    const uint8_t *sysFont = plat::systemFontBytes();
    if (sysFont && stbtt_InitFont(&g_fallbackFont, sysFont, 0)) {
        g_hasFallbackFont = true;
    }
}

void shutdownFonts() {
    for (auto &kv : g_atlases) if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    g_atlases.clear();
}

FontAtlas &getAtlas(Font f, float pxSize) {
    int qpx = std::clamp(static_cast<int>(std::round(pxSize)), 11, 150);
    uint32_t key = (static_cast<uint32_t>(f) << 16) | static_cast<uint32_t>(qpx);
    auto it = g_atlases.find(key);
    if (it != g_atlases.end()) return it->second;

    FontAtlas at;
    at.pxSize = static_cast<float>(qpx);
    at.info = g_fonts[static_cast<int>(f)];
    at.scale = stbtt_ScaleForPixelHeight(&at.info, at.pxSize);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&at.info, &asc, &desc, &gap);
    at.ascent = asc * at.scale;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
    stbtt_GetCodepointBitmapBox(&at.info, 'H', at.scale, at.scale, &cx0, &cy0, &cx1, &cy1);
    at.capHeight = (cy1 > cy0) ? static_cast<float>(cy1 - cy0) : (at.pxSize * 0.70f);

    int dim = (qpx >= 40) ? 2048 : 1024;
    at.dim = dim;
    std::vector<uint8_t> pixels(static_cast<size_t>(dim * dim), 0);

    std::vector<int> cps;
    for (int c = 32; c <= 126; ++c) cps.push_back(c);
    for (int c = 0x00A0; c <= 0x00FF; ++c) cps.push_back(c);
    const int extra[] = {0x2013, 0x2014, 0x2019, 0x2022, 0x2026, 0x2039, 0x203A, 0x2190, 0x2192, 0x2191, 0x2193};
    for (int c : extra) cps.push_back(c);

    std::vector<stbtt_packedchar> packed(cps.size());
    stbtt_pack_context pc{};
    stbtt_PackBegin(&pc, pixels.data(), dim, dim, 0, 2, nullptr);
    int ov = (qpx <= 44) ? 2 : 1;
    stbtt_PackSetOversampling(&pc, ov, ov);

    stbtt_pack_range range{};
    range.font_size = at.pxSize;
    range.array_of_unicode_codepoints = cps.data();
    range.num_chars = static_cast<int>(cps.size());
    range.chardata_for_range = packed.data();
    stbtt_PackFontRanges(&pc, fontBytes(f), 0, &range, 1);
    stbtt_PackEnd(&pc);

    int maxY = 2;
    for (size_t i = 0; i < cps.size(); ++i) {
        const auto &p = packed[i];
        if (p.y1 > maxY) maxY = p.y1;
        // Skip storing missing glyph slots if the primary font doesn't have the codepoint (allow fallback later!)
        if (cps[i] > 127 && stbtt_FindGlyphIndex(&at.info, cps[i]) == 0) continue;
        at.glyphs[static_cast<uint32_t>(cps[i])] = {
            p.xoff, p.yoff, p.xoff2, p.yoff2,
            static_cast<float>(p.x0) / dim, static_cast<float>(p.y0) / dim,
            static_cast<float>(p.x1) / dim, static_cast<float>(p.y1) / dim,
            p.xadvance,
        };
    }
    at.shelfX = 2;
    at.shelfY = maxY + 4;
    at.shelfH = 0;

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &at.tex);
    glBindTexture(GL_TEXTURE_2D, at.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, dim, dim, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    return g_atlases.emplace(key, std::move(at)).first->second;
}

const GlyphInfo *ensureGlyph(FontAtlas &at, uint32_t cp) {
    auto it = at.glyphs.find(cp);
    if (it != at.glyphs.end()) return &it->second;

    stbtt_fontinfo *src = nullptr;
    float sc = at.scale;
    int gIdx = stbtt_FindGlyphIndex(&at.info, static_cast<int>(cp));
    if (gIdx != 0) {
        src = &at.info;
    } else if (g_hasFallbackFont) {
        gIdx = stbtt_FindGlyphIndex(&g_fallbackFont, static_cast<int>(cp));
        if (gIdx != 0) {
            src = &g_fallbackFont;
            sc = stbtt_ScaleForPixelHeight(&g_fallbackFont, at.pxSize * 0.94f);
        }
    }
    if (!src) {
        auto q = at.glyphs.find('?');
        return (q != at.glyphs.end()) ? &q->second : nullptr;
    }

    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(src, gIdx, &adv, &lsb);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(src, gIdx, sc, sc, &x0, &y0, &x1, &y1);
    int gw = x1 - x0;
    int gh = y1 - y0;

    GlyphInfo gi{};
    gi.xadvance = adv * sc;

    if (gw > 0 && gh > 0 && gw < at.dim - 4 && gh < at.dim - 4) {
        if (at.shelfX + gw + 2 > at.dim) {
            at.shelfX = 2;
            at.shelfY += at.shelfH + 2;
            at.shelfH = 0;
        }
        if (at.shelfY + gh + 2 <= at.dim) {
            std::vector<uint8_t> bmp(static_cast<size_t>(gw * gh), 0);
            stbtt_MakeGlyphBitmap(src, bmp.data(), gw, gh, gw, sc, sc, gIdx);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, at.tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, at.shelfX, at.shelfY, gw, gh, GL_RED, GL_UNSIGNED_BYTE, bmp.data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

            gi.x0 = static_cast<float>(x0);
            gi.y0 = static_cast<float>(y0);
            gi.x1 = static_cast<float>(x1);
            gi.y1 = static_cast<float>(y1);
            gi.u0 = static_cast<float>(at.shelfX) / at.dim;
            gi.v0 = static_cast<float>(at.shelfY) / at.dim;
            gi.u1 = static_cast<float>(at.shelfX + gw) / at.dim;
            gi.v1 = static_cast<float>(at.shelfY + gh) / at.dim;

            at.shelfX += gw + 2;
            if (gh > at.shelfH) at.shelfH = gh;
        }
    }

    return &at.glyphs.emplace(cp, gi).first->second;
}

uint32_t nextUtf8(const char *&p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (!c) return 0;
    ++p;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && p[0]) {
        uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(p[0]) & 0x3F);
        p += 1; return cp;
    }
    if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
        uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(p[0]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(p[1]) & 0x3F);
        p += 2; return cp;
    }
    if ((c & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
        uint32_t cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(p[0]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) | (static_cast<unsigned char>(p[2]) & 0x3F);
        p += 3; return cp;
    }
    return '?';
}

void extractDominantColors(const uint8_t *rgba, int w, int h, Color &colA, Color &colB) {
    double sumR = 0, sumG = 0, sumB = 0, wSum = 0;
    double bestScore = -1.0;
    Color best = hex(0xF5A4C4);
    int step = std::max(1, (w * h) / 2048);

    for (int i = 0; i < w * h; i += step) {
        float r = rgba[i * 4 + 0] / 255.0f;
        float g = rgba[i * 4 + 1] / 255.0f;
        float b = rgba[i * 4 + 2] / 255.0f;
        float mx = std::max({r, g, b});
        float mn = std::min({r, g, b});
        float sat = (mx > 0.001f) ? (mx - mn) / mx : 0.0f;
        float luma = 0.299f * r + 0.587f * g + 0.114f * b;
        float weight = (0.15f + sat * sat * 2.5f) * (luma > 0.14f && luma < 0.90f ? 1.0f : 0.22f);
        sumR += r * weight; sumG += g * weight; sumB += b * weight; wSum += weight;
        double score = sat * 1.6 + (1.0 - std::fabs(luma - 0.55f)) * 0.6;
        if (score > bestScore && luma > 0.20f) { bestScore = score; best = {r, g, b, 1.0f}; }
    }
    if (wSum > 0.01) {
        colA = {static_cast<float>(sumR / wSum), static_cast<float>(sumG / wSum), static_cast<float>(sumB / wSum), 1.0f};
        if (std::max({colA.r, colA.g, colA.b}) < 0.45f) colA = lerpColor(colA, best, 0.65f);
        colB = {std::clamp(best.r * 0.72f + 0.06f, 0.05f, 1.0f),
                std::clamp(best.g * 0.72f + 0.05f, 0.05f, 1.0f),
                std::clamp(best.b * 0.80f + 0.08f, 0.08f, 1.0f), 1.0f};
    } else {
        colA = hex(0xF5A4C4);
        colB = hex(0x9E446A);
    }
}

}  // namespace

void Texture::destroy() { if (id) { glDeleteTextures(1, &id); id = 0; } w = h = 0; }

Texture createTextureRGBA(int w, int h, const uint8_t *rgba, bool linear) {
    Texture t; t.w = w; t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

Texture decodeImageMemory(const uint8_t *bytes, size_t len, Color &outColorA, Color &outColorB) {
    int w = 0, h = 0, ch = 0;
    unsigned char *px = stbi_load_from_memory(bytes, static_cast<int>(len), &w, &h, &ch, 4);
    if (!px) return Texture{};
    extractDominantColors(px, w, h, outColorA, outColorB);
    Texture t = createTextureRGBA(w, h, px, true);
    stbi_image_free(px);
    return t;
}

bool Renderer::init(int w, int h) {
    fbW_ = w; fbH_ = h;
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs); glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    locScreen_ = glGetUniformLocation(prog_, "uScreen");
    locTex_ = glGetUniformLocation(prog_, "uTex");
    locBlur_ = glGetUniformLocation(prog_, "uBlurTex");

    GLuint pvs = compileShader(GL_VERTEX_SHADER, kPostVS);
    GLuint pfs = compileShader(GL_FRAGMENT_SHADER, kPostFS);
    if (pvs && pfs) {
        progPost_ = glCreateProgram();
        glAttachShader(progPost_, pvs); glAttachShader(progPost_, pfs); glLinkProgram(progPost_);
        glDeleteShader(pvs); glDeleteShader(pfs);
        locPostTex_ = glGetUniformLocation(progPost_, "uTex");
        locPostTexel_ = glGetUniformLocation(progPost_, "uTexel");
        locPostOffset_ = glGetUniformLocation(progPost_, "uOffset");
        glGenVertexArrays(1, &vaoPost_);
    }

    // Create scene FBO + 1/4-res Kawase blur ping-pong FBOs (480x270)
    bool okScene = makeFbo(w, h, fboScene_, texScene_);
    bool okA = makeFbo(w / 4, h / 4, fboBlurA_, texBlurA_);
    bool okB = makeFbo(w / 4, h / 4, fboBlurB_, texBlurB_);
    hasBackdrop_ = okScene && okA && okB && (progPost_ != 0);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 65536 * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

    constexpr GLsizei s = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, mode)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, p0)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void *>(offsetof(Vertex, e0)));

    verts_.reserve(8192);
    alphaStack_.push_back(1.0f);
    initFonts();
    return true;
}

void Renderer::shutdown() {
    shutdownFonts();
    if (fboScene_) glDeleteFramebuffers(1, &fboScene_);
    if (fboBlurA_) glDeleteFramebuffers(1, &fboBlurA_);
    if (fboBlurB_) glDeleteFramebuffers(1, &fboBlurB_);
    if (texScene_) glDeleteTextures(1, &texScene_);
    if (texBlurA_) glDeleteTextures(1, &texBlurA_);
    if (texBlurB_) glDeleteTextures(1, &texBlurB_);
    if (vaoPost_) glDeleteVertexArrays(1, &vaoPost_);
    if (progPost_) glDeleteProgram(progPost_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
}

void Renderer::beginScene(Color clearCol) {
    glBindFramebuffer(GL_FRAMEBUFFER, hasBackdrop_ ? fboScene_ : 0);
    glViewport(0, 0, fbW_, fbH_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(clearCol.r, clearCol.g, clearCol.b, clearCol.a);
    glClear(GL_COLOR_BUFFER_BIT);
    alphaStack_.assign(1, 1.0f);
    clipStack_.clear();
    verts_.clear();
    boundTex_ = 0;
}

void Renderer::captureBackdropAndPresentScene() {
    flush();
    if (!hasBackdrop_) return;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(progPost_);
    glBindVertexArray(vaoPost_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(locPostTex_, 0);

    int bw = fbW_ / 4, bh = fbH_ / 4;

    // Pass 1: Scene -> BlurA (downsample + wide Kawase blur)
    glBindFramebuffer(GL_FRAMEBUFFER, fboBlurA_);
    glViewport(0, 0, bw, bh);
    glBindTexture(GL_TEXTURE_2D, texScene_);
    glUniform2f(locPostTexel_, 1.0f / bw, 1.0f / bh);
    glUniform1f(locPostOffset_, 1.6f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Pass 2: BlurA -> BlurB (wider blur)
    glBindFramebuffer(GL_FRAMEBUFFER, fboBlurB_);
    glBindTexture(GL_TEXTURE_2D, texBlurA_);
    glUniform1f(locPostOffset_, 2.8f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Pass 3: BlurB -> BlurA (creamy silky frost!)
    glBindFramebuffer(GL_FRAMEBUFFER, fboBlurA_);
    glBindTexture(GL_TEXTURE_2D, texBlurB_);
    glUniform1f(locPostOffset_, 4.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Present sharp scene to default framebuffer (0)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW_, fbH_);
    glBindTexture(GL_TEXTURE_2D, texScene_);
    glUniform1f(locPostOffset_, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Re-enable blend for Liquid Glass overlays!
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    boundTex_ = 0;
}

void Renderer::endFrame() { flush(); }

void Renderer::flush() {
    if (verts_.empty()) return;
    glUseProgram(prog_);
    glUniform2f(locScreen_, static_cast<float>(fbW_), static_cast<float>(fbH_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, boundTex_);
    glUniform1i(locTex_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasBackdrop_ ? texBlurA_ : 0);
    glUniform1i(locBlur_, 1);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = verts_.size() * sizeof(Vertex);
    if (bytes > 65536 * sizeof(Vertex)) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), verts_.data(), GL_DYNAMIC_DRAW);
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), verts_.data());
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    verts_.clear();
}

void Renderer::pushAlpha(float a) { alphaStack_.push_back(alphaStack_.back() * a); }
void Renderer::popAlpha() { if (alphaStack_.size() > 1) alphaStack_.pop_back(); }

void Renderer::pushClip(float x, float y, float w, float h) {
    flush();
    Rect r{x, y, std::max(0.0f, w), std::max(0.0f, h)};
    if (!clipStack_.empty()) {
        const Rect &p = clipStack_.back();
        float x0 = std::max(r.x, p.x), y0 = std::max(r.y, p.y);
        float x1 = std::min(r.x + r.w, p.x + p.w), y1 = std::min(r.y + r.h, p.y + p.h);
        r = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    clipStack_.push_back(r);
    glEnable(GL_SCISSOR_TEST);
    glScissor(std::max(0, static_cast<int>(r.x)), std::max(0, fbH_ - static_cast<int>(r.y + r.h)),
              std::max(0, static_cast<int>(r.w)), std::max(0, static_cast<int>(r.h)));
}

void Renderer::popClip() {
    flush();
    if (!clipStack_.empty()) clipStack_.pop_back();
    if (clipStack_.empty()) glDisable(GL_SCISSOR_TEST);
    else {
        const Rect &r = clipStack_.back();
        glScissor(std::max(0, static_cast<int>(r.x)), std::max(0, fbH_ - static_cast<int>(r.y + r.h)),
                  std::max(0, static_cast<int>(r.w)), std::max(0, static_cast<int>(r.h)));
    }
}

Color Renderer::applyAlpha(Color c) const { c.a *= alphaStack_.back(); return c; }

void Renderer::emitQuad(const Vertex &v0, const Vertex &v1, const Vertex &v2, const Vertex &v3, GLuint tex) {
    if (tex != 0 && tex != boundTex_) { flush(); boundTex_ = tex; }
    if (verts_.size() + 6 > 60000) flush();
    verts_.push_back(v0); verts_.push_back(v1); verts_.push_back(v2);
    verts_.push_back(v0); verts_.push_back(v2); verts_.push_back(v3);
}

void Renderer::fillRect(float x, float y, float w, float h, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || w <= 0 || h <= 0) return;
    emitQuad({x, y, 0, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::fillRectGradV(float x, float y, float w, float h, Color top, Color bot) {
    top = applyAlpha(top); bot = applyAlpha(bot);
    emitQuad({x, y, 0, 0, top.r, top.g, top.b, top.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, top.r, top.g, top.b, top.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, bot.r, bot.g, bot.b, bot.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, bot.r, bot.g, bot.b, bot.a, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::fillRectGradH(float x, float y, float w, float h, Color left, Color right) {
    left = applyAlpha(left); right = applyAlpha(right);
    emitQuad({x, y, 0, 0, left.r, left.g, left.b, left.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, right.r, right.g, right.b, right.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, right.r, right.g, right.b, right.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, left.r, left.g, left.b, left.a, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::fillRectGradDiag(float x, float y, float w, float h, Color tl, Color br) {
    tl = applyAlpha(tl); br = applyAlpha(br);
    Color mid = lerpColor(tl, br, 0.5f);
    emitQuad({x, y, 0, 0, tl.r, tl.g, tl.b, tl.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, mid.r, mid.g, mid.b, mid.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, br.r, br.g, br.b, br.a, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, mid.r, mid.g, mid.b, mid.a, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::drawGlowOrb(float cx, float cy, float rx, float ry, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f) return;
    emitQuad({cx - rx, cy - ry, 0, 0, c.r, c.g, c.b, c.a, 4, 0, 0, 0, 0, 0, 0, 0, 0},
             {cx + rx, cy - ry, 1, 0, c.r, c.g, c.b, c.a, 4, 0, 0, 0, 0, 0, 0, 0, 0},
             {cx + rx, cy + ry, 1, 1, c.r, c.g, c.b, c.a, 4, 0, 0, 0, 0, 0, 0, 0, 0},
             {cx - rx, cy + ry, 0, 1, c.r, c.g, c.b, c.a, 4, 0, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::drawShadow(float x, float y, float w, float h, float blur, float radius, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    float bx = x - blur, by = y - blur, bw = w + blur * 2.0f, bh = h + blur * 2.0f;
    emitQuad({bx, by, 0, 0, c.r, c.g, c.b, c.a, 6, hw, hh, blur, radius, 0, 0, 0, 0},
             {bx + bw, by, 1, 0, c.r, c.g, c.b, c.a, 6, hw, hh, blur, radius, 0, 0, 0, 0},
             {bx + bw, by + bh, 1, 1, c.r, c.g, c.b, c.a, 6, hw, hh, blur, radius, 0, 0, 0, 0},
             {bx, by + bh, 0, 1, c.r, c.g, c.b, c.a, 6, hw, hh, blur, radius, 0, 0, 0, 0}, 0);
}

void Renderer::drawLiquidGlass(float x, float y, float w, float h, float radius, Color tint, float activeGlow, Color rimTint) {
    tint = applyAlpha(tint);
    if (w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = std::min(radius, std::min(hw, hh));
    emitQuad({x, y, 0, 0, tint.r, tint.g, tint.b, tint.a, 5, hw, hh, rad, activeGlow, rimTint.r, rimTint.g, rimTint.b, rimTint.a},
             {x + w, y, 1, 0, tint.r, tint.g, tint.b, tint.a, 5, hw, hh, rad, activeGlow, rimTint.r, rimTint.g, rimTint.b, rimTint.a},
             {x + w, y + h, 1, 1, tint.r, tint.g, tint.b, tint.a, 5, hw, hh, rad, activeGlow, rimTint.r, rimTint.g, rimTint.b, rimTint.a},
             {x, y + h, 0, 1, tint.r, tint.g, tint.b, tint.a, 5, hw, hh, rad, activeGlow, rimTint.r, rimTint.g, rimTint.b, rimTint.a}, 0);
}

void Renderer::drawSoftCard(float x, float y, float w, float h, float radius, Color fill, Color subtleBorder) {
    fillRoundedRect(x, y, w, h, radius, fill);
    if (subtleBorder.a > 0.005f) strokeRoundedRect(x, y, w, h, radius, 1.0f, subtleBorder);
}

void Renderer::drawGradientPill(float x, float y, float w, float h, float radius, Color left, Color right) {
    left = applyAlpha(left); right = applyAlpha(right);
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = std::min(radius, std::min(hw, hh));
    emitQuad({x, y, 0, 0, left.r, left.g, left.b, left.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, right.r, right.g, right.b, right.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, right.r, right.g, right.b, right.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, left.r, left.g, left.b, left.a, 3, hw, hh, rad, 0, 0, 0, 0, 0}, 0);
}

void Renderer::drawRoundedTexture(const Texture &tex, float x, float y, float w, float h, float radius,
                                  Color tint, float gloss) {
    tint = applyAlpha(tint);
    if (!tex.id || tint.a <= 0.001f || w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = std::min(radius, std::min(hw, hh));
    emitQuad({x, y, 0, 0, tint.r, tint.g, tint.b, tint.a, 1, hw, hh, rad, gloss, 0, 0, 0, 0},
             {x + w, y, 1, 0, tint.r, tint.g, tint.b, tint.a, 1, hw, hh, rad, gloss, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, tint.r, tint.g, tint.b, tint.a, 1, hw, hh, rad, gloss, 0, 0, 0, 0},
             {x, y + h, 0, 1, tint.r, tint.g, tint.b, tint.a, 1, hw, hh, rad, gloss, 0, 0, 0, 0}, tex.id);
}

void Renderer::fillRoundedRect(float x, float y, float w, float h, float radius, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = std::min(radius, std::min(hw, hh));
    emitQuad({x, y, 0, 0, c.r, c.g, c.b, c.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x + w, y, 1, 0, c.r, c.g, c.b, c.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, c.r, c.g, c.b, c.a, 3, hw, hh, rad, 0, 0, 0, 0, 0},
             {x, y + h, 0, 1, c.r, c.g, c.b, c.a, 3, hw, hh, rad, 0, 0, 0, 0, 0}, 0);
}

void Renderer::strokeRoundedRect(float x, float y, float w, float h, float radius, float thickness, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = std::min(radius, std::min(hw, hh));
    emitQuad({x, y, 0, 0, c.r, c.g, c.b, c.a, 3, hw, hh, rad, thickness, 0, 0, 0, 0},
             {x + w, y, 1, 0, c.r, c.g, c.b, c.a, 3, hw, hh, rad, thickness, 0, 0, 0, 0},
             {x + w, y + h, 1, 1, c.r, c.g, c.b, c.a, 3, hw, hh, rad, thickness, 0, 0, 0, 0},
             {x, y + h, 0, 1, c.r, c.g, c.b, c.a, 3, hw, hh, rad, thickness, 0, 0, 0, 0}, 0);
}

void Renderer::fillCircle(float cx, float cy, float r, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || r <= 0) return;
    float p = r + 2.0f;
    emitQuad({cx - p, cy - p, 0, 0, c.r, c.g, c.b, c.a, 7, r, 0, 0, 0, 0, 0, 0, 0},
             {cx + p, cy - p, 1, 0, c.r, c.g, c.b, c.a, 7, r, 0, 0, 0, 0, 0, 0, 0},
             {cx + p, cy + p, 1, 1, c.r, c.g, c.b, c.a, 7, r, 0, 0, 0, 0, 0, 0, 0},
             {cx - p, cy + p, 0, 1, c.r, c.g, c.b, c.a, 7, r, 0, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::strokeCircle(float cx, float cy, float r, float thickness, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f || r <= 0) return;
    float p = r + 2.0f;
    emitQuad({cx - p, cy - p, 0, 0, c.r, c.g, c.b, c.a, 7, r, thickness, 0, 0, 0, 0, 0, 0},
             {cx + p, cy - p, 1, 0, c.r, c.g, c.b, c.a, 7, r, thickness, 0, 0, 0, 0, 0, 0},
             {cx + p, cy + p, 1, 1, c.r, c.g, c.b, c.a, 7, r, thickness, 0, 0, 0, 0, 0, 0},
             {cx - p, cy + p, 0, 1, c.r, c.g, c.b, c.a, 7, r, thickness, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::drawLine(float x0, float y0, float x1, float y1, float thickness, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (len < 0.001f) { fillCircle(x0, y0, thickness * 0.5f, c); return; }
    float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
    float r = thickness * 0.5f, pad = r + 1.5f;
    float sx = x0 - ux * pad, sy = y0 - uy * pad;
    float ex = x1 + ux * pad, ey = y1 + uy * pad;
    emitQuad({sx - nx * pad, sy - ny * pad, 0, 0, c.r, c.g, c.b, c.a, 8, len, r, 0, 0, 0, 0, 0, 0},
             {ex - nx * pad, ey - ny * pad, 1, 0, c.r, c.g, c.b, c.a, 8, len, r, 0, 0, 0, 0, 0, 0},
             {ex + nx * pad, ey + ny * pad, 1, 1, c.r, c.g, c.b, c.a, 8, len, r, 0, 0, 0, 0, 0, 0},
             {sx + nx * pad, sy + ny * pad, 0, 1, c.r, c.g, c.b, c.a, 8, len, r, 0, 0, 0, 0, 0, 0}, 0);
}

void Renderer::drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    c = applyAlpha(c);
    if (c.a <= 0.001f) return;
    if (verts_.size() + 3 > 60000) flush();
    verts_.push_back({x0, y0, 0, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    verts_.push_back({x1, y1, 1, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    verts_.push_back({x2, y2, 0, 1, c.r, c.g, c.b, c.a, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}

void Renderer::drawUiIcon(UiIcon ic, float cx, float cy, float s, Color c) {
    float h = s * 0.5f;
    float t = std::max(1.8f, s * 0.10f);
    switch (ic) {
        case UiIcon::Play:
            drawTriangle(cx - h * 0.42f, cy - h * 0.62f, cx + h * 0.68f, cy, cx - h * 0.42f, cy + h * 0.62f, c);
            break;
        case UiIcon::Home:
            drawLine(cx - h * 0.85f, cy - h * 0.05f, cx, cy - h * 0.82f, t, c);
            drawLine(cx, cy - h * 0.82f, cx + h * 0.85f, cy - h * 0.05f, t, c);
            drawLine(cx - h * 0.62f, cy - h * 0.12f, cx - h * 0.62f, cy + h * 0.78f, t, c);
            drawLine(cx + h * 0.62f, cy - h * 0.12f, cx + h * 0.62f, cy + h * 0.78f, t, c);
            drawLine(cx - h * 0.62f, cy + h * 0.78f, cx + h * 0.62f, cy + h * 0.78f, t, c);
            break;
        case UiIcon::Apps: {
            float b = s * 0.36f, g = s * 0.12f;
            strokeRoundedRect(cx - b - g * 0.5f, cy - b - g * 0.5f, b, b, 3.0f, t, c);
            strokeRoundedRect(cx + g * 0.5f, cy - b - g * 0.5f, b, b, 3.0f, t, c);
            strokeRoundedRect(cx - b - g * 0.5f, cy + g * 0.5f, b, b, 3.0f, t, c);
            strokeRoundedRect(cx + g * 0.5f, cy + g * 0.5f, b, b, 3.0f, t, c);
            break;
        }
        case UiIcon::Folder:
            strokeRoundedRect(cx - h * 0.82f, cy - h * 0.48f, s * 0.82f, s * 0.64f, 4.0f, t, c);
            drawLine(cx - h * 0.76f, cy - h * 0.62f, cx - h * 0.10f, cy - h * 0.62f, t, c);
            break;
        case UiIcon::Ftp:
            drawLine(cx, cy - h * 0.76f, cx, cy + h * 0.58f, t, c);
            drawLine(cx - h * 0.45f, cy + h * 0.14f, cx, cy + h * 0.62f, t, c);
            drawLine(cx + h * 0.45f, cy + h * 0.14f, cx, cy + h * 0.62f, t, c);
            drawLine(cx - h * 0.72f, cy + h * 0.82f, cx + h * 0.72f, cy + h * 0.82f, t, c);
            break;
        case UiIcon::Settings:
            strokeCircle(cx, cy, h * 0.42f, t, c);
            for (int k = 0; k < 8; ++k) {
                float a = k * 0.785398f;
                drawLine(cx + std::cos(a) * h * 0.56f, cy + std::sin(a) * h * 0.56f,
                         cx + std::cos(a) * h * 0.82f, cy + std::sin(a) * h * 0.82f, t * 1.15f, c);
            }
            break;
        case UiIcon::Search:
            strokeCircle(cx - h * 0.14f, cy - h * 0.14f, h * 0.50f, t, c);
            drawLine(cx + h * 0.24f, cy + h * 0.24f, cx + h * 0.78f, cy + h * 0.78f, t * 1.2f, c);
            break;
        case UiIcon::Sort:
            drawLine(cx - h * 0.72f, cy - h * 0.44f, cx + h * 0.72f, cy - h * 0.44f, t, c);
            drawLine(cx - h * 0.72f, cy, cx + h * 0.34f, cy, t, c);
            drawLine(cx - h * 0.72f, cy + h * 0.44f, cx - h * 0.06f, cy + h * 0.44f, t, c);
            break;
        case UiIcon::ArrowLeft:
            drawLine(cx - h * 0.60f, cy, cx + h * 0.65f, cy, t, c);
            drawLine(cx - h * 0.62f, cy, cx - h * 0.12f, cy - h * 0.48f, t, c);
            drawLine(cx - h * 0.62f, cy, cx - h * 0.12f, cy + h * 0.48f, t, c);
            break;
        case UiIcon::ArrowRight:
            drawLine(cx - h * 0.65f, cy, cx + h * 0.60f, cy, t, c);
            drawLine(cx + h * 0.62f, cy, cx + h * 0.12f, cy - h * 0.48f, t, c);
            drawLine(cx + h * 0.62f, cy, cx + h * 0.12f, cy + h * 0.48f, t, c);
            break;
    }
}

void Renderer::drawStar(float cx, float cy, float r, bool filled, Color c) {
    float ptsX[10], ptsY[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.6283185f;
        float rad = (i % 2 == 0) ? r : r * 0.44f;
        ptsX[i] = cx + std::cos(a) * rad;
        ptsY[i] = cy + std::sin(a) * rad;
    }
    if (filled) {
        fillCircle(cx, cy, r * 0.42f, c);
        for (int i = 0; i < 5; ++i) drawLine(cx, cy, ptsX[i * 2], ptsY[i * 2], r * 0.42f, c);
    } else {
        for (int i = 0; i < 10; ++i) {
            int n = (i + 1) % 10;
            drawLine(ptsX[i], ptsY[i], ptsX[n], ptsY[n], 1.8f, c);
        }
    }
}

void Renderer::drawYoruMoon(float cx, float cy, float r, Color c) {
    // Crisp pastel crescent facing top-right + tiny 4-point star cleanly inside the open hollow
    constexpr int kSeg = 24;
    // Screen coords (+Y down): 0.72 rad = bottom-right, 1.57 = bottom, 3.14 = left, 4.52 = top
    float startA = 0.72f, endA = 4.52f;
    float arcR = r * 0.74f;
    for (int i = 0; i < kSeg; ++i) {
        float t0 = static_cast<float>(i) / kSeg;
        float t1 = static_cast<float>(i + 1) / kSeg;
        float tm = (t0 + t1) * 0.5f;
        float a0 = startA + (endA - startA) * t0;
        float a1 = startA + (endA - startA) * t1;
        float thick = std::sin(tm * 3.1415926f) * (r * 0.44f) + 1.3f;
        drawLine(cx + std::cos(a0) * arcR, cy + std::sin(a0) * arcR,
                 cx + std::cos(a1) * arcR, cy + std::sin(a1) * arcR, thick, c);
    }
    // Delicate 4-point sparkle nestled cleanly in the open top-right hollow (never overlapping the moon!)
    float sx = cx + r * 0.44f, sy = cy - r * 0.38f;
    float sr = r * 0.28f;
    drawTriangle(sx, sy - sr, sx + sr * 0.28f, sy, sx - sr * 0.28f, sy, c);
    drawTriangle(sx, sy + sr, sx + sr * 0.28f, sy, sx - sr * 0.28f, sy, c);
    drawTriangle(sx - sr, sy, sx, sy - sr * 0.28f, sx, sy + sr * 0.28f, c);
    drawTriangle(sx + sr, sy, sx, sy - sr * 0.28f, sx, sy + sr * 0.28f, c);
}

void Renderer::drawBattery(float x, float y, float w, float h, int pct, bool charging, Color c) {
    pct = std::clamp(pct, 0, 100);
    strokeRoundedRect(x, y, w - 4.0f, h, 4.0f, 1.8f, c);
    fillRoundedRect(x + w - 3.5f, y + h * 0.28f, 3.5f, h * 0.44f, 1.5f, c);
    float innerW = std::max(0.0f, (w - 10.0f) * (pct / 100.0f));
    Color fill = charging ? hex(0x6EE7B7) : (pct <= 15 ? hex(0xFCA5A5) : c);
    fillRoundedRect(x + 3.0f, y + 3.0f, innerW, h - 6.0f, 2.0f, fill);
}

void Renderer::drawCursor(float x, float y) {
    fillCircle(x, y, 11.0f, rgba(255, 220, 236, 0.24f));
    strokeCircle(x, y, 10.0f, 1.8f, rgba(255, 255, 255, 0.88f));
    fillCircle(x, y, 2.8f, hex(0xFFFFFF));
}

float Renderer::measureText(Font f, float pxSize, const char *utf8) {
    if (!utf8 || !*utf8) return 0.0f;
    FontAtlas &at = getAtlas(f, pxSize);
    float x = 0.0f;
    uint32_t prev = 0;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = nextUtf8(p);
        if (prev && prev < 0x3000 && cp < 0x3000) {
            x += stbtt_GetCodepointKernAdvance(&at.info, static_cast<int>(prev), static_cast<int>(cp)) * at.scale;
        }
        const GlyphInfo *g = ensureGlyph(at, cp);
        if (g) x += g->xadvance;
        prev = cp;
    }
    return x;
}

void Renderer::drawText(Font f, float pxSize, float x, float y, const char *utf8, Color c, Align align) {
    c = applyAlpha(c);
    if (!utf8 || !*utf8 || c.a <= 0.001f) return;
    FontAtlas &at = getAtlas(f, pxSize);
    if (align == Align::Center) x -= measureText(f, pxSize, utf8) * 0.5f;
    else if (align == Align::Right) x -= measureText(f, pxSize, utf8);

    float penX = std::floor(x + 0.5f);
    float base = std::floor(y + at.ascent + 0.5f);
    uint32_t prev = 0;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = nextUtf8(p);
        if (prev && prev < 0x3000 && cp < 0x3000) {
            penX += stbtt_GetCodepointKernAdvance(&at.info, static_cast<int>(prev), static_cast<int>(cp)) * at.scale;
        }
        const GlyphInfo *g = ensureGlyph(at, cp);
        if (g) {
            if (g->x1 > g->x0 && g->y1 > g->y0) {
                emitQuad({penX + g->x0, base + g->y0, g->u0, g->v0, c.r, c.g, c.b, c.a, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                         {penX + g->x1, base + g->y0, g->u1, g->v0, c.r, c.g, c.b, c.a, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                         {penX + g->x1, base + g->y1, g->u1, g->v1, c.r, c.g, c.b, c.a, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                         {penX + g->x0, base + g->y1, g->u0, g->v1, c.r, c.g, c.b, c.a, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                         at.tex);
            }
            penX += g->xadvance;
        }
        prev = cp;
    }
}

void Renderer::drawTextBoxed(Font f, float pxSize, float x, float rectY, float rectH, const char *utf8, Color c,
                             Align align) {
    if (!utf8 || !*utf8) return;
    const FontAtlas &at = getAtlas(f, pxSize);
    float baseline = rectY + (rectH + at.capHeight) * 0.5f;
    drawText(f, pxSize, x, baseline - at.ascent, utf8, c, align);
}

void Renderer::drawTextClipped(Font f, float pxSize, float x, float y, float maxW, const char *utf8, Color c,
                               Align align) {
    if (!utf8 || !*utf8 || maxW <= 4.0f) return;
    if (measureText(f, pxSize, utf8) <= maxW) {
        drawText(f, pxSize, x, y, utf8, c, align);
        return;
    }
    std::string s(utf8);
    while (!s.empty()) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
        std::string cand = s + "…";
        if (measureText(f, pxSize, cand.c_str()) <= maxW) {
            drawText(f, pxSize, x, y, cand.c_str(), c, align);
            return;
        }
    }
}

void Renderer::drawTextCleanReveal(Font f, float pxSize, float x, float y, float maxW, const char *utf8, Color c,
                                   float progress) {
    // Clean, understated vertical slide-up + fade (zero diagonal flying!)
    float p = std::clamp(progress, 0.0f, 1.0f);
    float inv = 1.0f - p;
    float ease = 1.0f - inv * inv * inv;
    float offY = (1.0f - ease) * 16.0f;
    drawTextClipped(f, pxSize, x, y + offY, maxW, utf8, withAlpha(c, ease), Align::Left);
}

}  // namespace gfx
