// SPDX-License-Identifier: MIT
//
// A batched 2D renderer on OpenGL ES 3 (EGL on the default NWindow), with
// text from FreeType. Coordinates are in a 1280x720 logical space, scaled to
// the real surface size.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H

struct Color {
    uint8_t r, g, b, a;
    constexpr Color(uint8_t r_ = 0, uint8_t g_ = 0, uint8_t b_ = 0, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_)
    {
    }
    static constexpr Color hex(uint32_t rgb, uint8_t a = 255)
    {
        return Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
    }
};

// Rounded: rectangle with 6px corners. Rect: no mask.
enum class Shape { Circle, Squircle, Rounded, Rect };

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Renderer {
public:
    static constexpr float kWidth = 1280, kHeight = 720;

    bool init();
    void exit();

    void beginFrame(Color clear);
    void endFrame();

    void fillRect(Rect r, Color c);
    void fillRoundRect(Rect r, float radius, Color c);
    void fillCircle(float cx, float cy, float radius, Color c);
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);

    // Inline objects: codepoints in this private-use range draw nothing but
    // take up kInlineAdvance x the text size, so images (emotes) can be laid
    // out with text and drawn into the gaps (kInlineSize x the text size).
    static constexpr uint32_t kInlineFirst = 0xF0000, kInlineLast = 0xFFFFD;
    static constexpr float kInlineAdvance = 1.375f, kInlineSize = 1.25f;
    // Codepoints in this range are invisible and take no space: markers the
    // UI puts in text to find spans (mentions) after wrapping.
    static constexpr uint32_t kMarkFirst = 0x100000, kMarkLast = 0x10FFFD;

    // Text. size is the pixel size in logical units. Returns the advance.
    float drawText(float x, float y, std::string_view text, float size, Color c,
                   float max_width = 0);
    float measureText(std::string_view text, float size);
    float lineHeight(float size) const { return size * 1.3f; }
    float ascent(float size) const { return size * 0.95f; }
    // Word-wraps text to width. Explicit newlines are kept.
    std::vector<std::string> wrapText(std::string_view text, float size, float width);
    // Truncates with an ellipsis to fit width.
    std::string ellipsize(std::string_view text, float size, float width);

    // Draws an RGBA texture into r, masked to a circle or a squircle
    // (superellipse) with an antialiased edge.
    void drawImage(GLuint tex, Rect r, Shape shape, Color tint = Color(255, 255, 255));
    // Fills r with a solid colour in the given shape.
    void fillShape(Rect r, Shape shape, Color c);

    // Clip to a logical rectangle (nested clips intersect). Flushes the batch.
    void pushClip(Rect r);
    void popClip();

    float scale() const { return m_scale; }

private:
    struct Vertex {
        float x, y, u, v;
        uint8_t r, g, b, a;
    };
    struct Glyph {
        float u0, v0, u1, v1;
        int w, h, left, top;
        float advance;
    };

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    int m_surfaceW = 1280, m_surfaceH = 720;
    float m_scale = 1;

    GLuint m_program = 0, m_vao = 0, m_vbo = 0, m_atlasTex = 0;
    GLint m_uScreen = -1;
    GLuint m_imageProgram = 0;
    GLint m_uImgScreen = -1, m_uImgRect = -1, m_uImgShape = -1, m_uImgRadius = -1;
    GLuint m_whiteTex = 0;
    std::vector<Vertex> m_verts;
    std::vector<Rect> m_clips;

    // Glyph atlas (single channel). Rasterised at the surface's real pixel
    // size so text stays sharp at any resolution.
    static constexpr int kAtlasSize = 2048;
    std::vector<uint8_t> m_atlas;
    int m_penX = 1, m_penY = 1, m_rowH = 0;
    bool m_atlasDirty = false;
    std::unordered_map<uint64_t, Glyph> m_glyphs;

    FT_Library m_ft = nullptr;
    std::vector<FT_Face> m_faces;  // fallback chain
    std::vector<std::vector<uint8_t>> m_fontData;

    bool initFonts();
    const Glyph *glyph(uint32_t cp, int px);
    void resetAtlas();
    void flush();
    void applyClip();
    void pushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              Color c);
};
