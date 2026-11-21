#pragma once

#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gfx {

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

constexpr Color hex(uint32_t rgb, float a = 1.0f) {
    return {
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        a,
    };
}

constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, float a = 1.0f) {
    return {r / 255.0f, g / 255.0f, b / 255.0f, a};
}

inline Color withAlpha(Color c, float a) {
    c.a *= a;
    return c;
}

inline Color lerpColor(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

enum class Font {
    Serif = 0,        // InstrumentSerif-Regular
    SerifItalic = 1,  // InstrumentSerif-Italic (brand mark like Yoru!)
    Sans = 2,         // Outfit-SemiBold
    Semi = 3,         // Outfit-Bold
};

enum class Align { Left, Center, Right };

enum class UiIcon {
    Home,
    Apps,
    Folder,
    Ftp,
    Settings,
    Play,
    Search,
    Sort,
    ArrowLeft,
    ArrowRight,
};

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
    void destroy();
};

Texture createTextureRGBA(int w, int h, const uint8_t *rgba, bool linear = true);
Texture decodeImageMemory(const uint8_t *bytes, size_t len, Color &outColorA, Color &outColorB);

class Renderer {
public:
    bool init(int w, int h);
    void shutdown();

    // Renders scene into offscreen FBO so true real-time Liquid Glass backdrop blur can sample it!
    void beginScene(Color clearCol);
    // Blurs the rendered scene into the low-res blur FBO, then blits sharp scene to default framebuffer.
    // All subsequent drawLiquidGlass() calls sample the real blurred backdrop!
    void captureBackdropAndPresentScene();
    void endFrame();
    void flush();

    void pushAlpha(float a);
    void popAlpha();
    void pushClip(float x, float y, float w, float h);
    void popClip();

    // Backgrounds & Glows (dithered, zero banding)
    void fillRect(float x, float y, float w, float h, Color c);
    void fillRectGradV(float x, float y, float w, float h, Color top, Color bot);
    void fillRectGradH(float x, float y, float w, float h, Color left, Color right);
    void fillRectGradDiag(float x, float y, float w, float h, Color tl, Color br);
    void drawGlowOrb(float cx, float cy, float rx, float ry, Color c);

    // Authentic Real-Time Backdrop-Blurred Liquid Glass (NO hard AI-slop outlines!)
    void drawShadow(float x, float y, float w, float h, float blur, float radius, Color c);
    void drawLiquidGlass(float x, float y, float w, float h, float radius, Color tint, float activeGlow = 0.0f,
                         Color rimTint = hex(0xFFFFFF, 0.22f));
    void drawSoftCard(float x, float y, float w, float h, float radius, Color fill, Color subtleBorder);
    void drawGradientPill(float x, float y, float w, float h, float radius, Color left, Color right);

    // Rounded textures (for .nro icons & Hero blurred art)
    void drawRoundedTexture(const Texture &tex, float x, float y, float w, float h, float radius,
                            Color tint = {1, 1, 1, 1}, float gloss = 0.08f);

    // Primitives & Crisp Vector UI Icons (zero missing Unicode glyphs!)
    void fillRoundedRect(float x, float y, float w, float h, float radius, Color c);
    void strokeRoundedRect(float x, float y, float w, float h, float radius, float thickness, Color c);
    void fillCircle(float cx, float cy, float r, Color c);
    void strokeCircle(float cx, float cy, float r, float thickness, Color c);
    void drawLine(float x0, float y0, float x1, float y1, float thickness, Color c);
    void drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
    void drawUiIcon(UiIcon ic, float cx, float cy, float size, Color c);
    void drawStar(float cx, float cy, float r, bool filled, Color c);
    void drawYoruMoon(float cx, float cy, float r, Color c);
    void drawBattery(float x, float y, float w, float h, int pct, bool charging, Color c);
    void drawCursor(float x, float y);

    // TrueType AA Text
    float measureText(Font f, float pxSize, const char *utf8);
    void drawText(Font f, float pxSize, float x, float y, const char *utf8, Color c, Align align = Align::Left);
    void drawTextBoxed(Font f, float pxSize, float x, float rectY, float rectH, const char *utf8, Color c,
                       Align align = Align::Left);
    void drawTextClipped(Font f, float pxSize, float x, float y, float maxW, const char *utf8, Color c,
                         Align align = Align::Left);
    // Clean, subtle vertical kinetic text reveal (NO diagonal flying!)
    void drawTextCleanReveal(Font f, float pxSize, float x, float y, float maxW, const char *utf8, Color c,
                             float progress);

private:
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
        float mode;
        float p0, p1, p2, p3;
        float e0, e1, e2, e3;  // extra tint/rim color for Liquid Glass!
    };

    int fbW_ = 1920, fbH_ = 1080;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
    GLint locScreen_ = -1, locTex_ = -1, locBlur_ = -1;

    // Offscreen scene + dual Kawase blur FBOs for authentic macOS/Yoru Liquid Glass
    GLuint fboScene_ = 0, texScene_ = 0;
    GLuint fboBlurA_ = 0, texBlurA_ = 0;
    GLuint fboBlurB_ = 0, texBlurB_ = 0;
    GLuint progPost_ = 0, vaoPost_ = 0;
    GLint locPostTex_ = -1, locPostTexel_ = -1, locPostOffset_ = -1;
    bool hasBackdrop_ = false;

    GLuint boundTex_ = 0;
    std::vector<Vertex> verts_;
    std::vector<float> alphaStack_;
    std::vector<Rect> clipStack_;

    void emitQuad(const Vertex &v0, const Vertex &v1, const Vertex &v2, const Vertex &v3, GLuint tex);
    Color applyAlpha(Color c) const;
};

}  // namespace gfx
