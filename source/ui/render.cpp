// SPDX-License-Identifier: MIT
#include "ui/render.h"

#include "util/sysinfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <switch.h>

#include "util/assets.h"
#include "util/util.h"

static const char *const kVertexSource =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform vec2 u_screen;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    vec2 ndc = a_pos / u_screen * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

// The atlas texel at (0,0) is opaque white, used for solid shapes.
static const char *const kFragmentSource =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_atlas;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float a = texture(u_atlas, v_uv).r;\n"
    "    o_color = vec4(v_color.rgb, v_color.a * a);\n"
    "}\n";

// Images, masked to a shape with an antialiased edge. The mask is computed
// from the fragment's position relative to the target rectangle:
//   circle:   |p| = 1
//   squircle: |x|^5 + |y|^5 = 1 (a superellipse)
//   rounded:  a rectangle with corners of radius u_radius (in pixels)
// The distance is converted to pixels so the edge is one pixel wide.
// Uniforms shared with the vertex shader must match its precision (highp),
// or the program fails to link.
static const char *const kImageFragmentSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_image;\n"
    "uniform vec4 u_rect;\n"   // x, y, w, h in window pixels (top-left origin)
    "uniform int u_shape;\n"   // 0 circle, 1 squircle, 2 rounded rectangle
    "uniform float u_radius;\n"
    "uniform vec2 u_screen;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec2 half_size = u_rect.zw * 0.5;\n"
    "    vec2 frag = vec2(gl_FragCoord.x, u_screen.y - gl_FragCoord.y);\n"
    "    vec2 p = abs(frag - (u_rect.xy + half_size)) / half_size;\n"
    "    float mask;\n"
    "    if (u_shape == 2) {\n"
    "        vec2 q = abs(frag - (u_rect.xy + half_size)) - half_size + vec2(u_radius);\n"
    "        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;\n"
    "        mask = clamp(0.5 - dist, 0.0, 1.0);\n"
    "    } else {\n"
    "        float d;\n"
    "        if (u_shape == 0) {\n"
    "            d = length(p) - 1.0;\n"
    "        } else {\n"
    "            vec2 q = p * p;\n"
    "            d = pow(q.x * q.x * p.x + q.y * q.y * p.y, 0.2) - 1.0;\n"
    "        }\n"
    "        mask = clamp(0.5 - d * min(half_size.x, half_size.y), 0.0, 1.0);\n"
    "    }\n"
    "    vec4 c = texture(u_image, v_uv) * v_color;\n"
    "    o_color = vec4(c.rgb, c.a * mask);\n"
    "}\n";

// Video frames: NV12 (a luma texture and an interleaved chroma texture) to
// RGB, limited range, BT.709 or BT.601.
static const char *const kNv12FragmentSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_y;\n"
    "uniform sampler2D u_uv;\n"
    "uniform int u_bt709;\n"
    "uniform vec2 u_screen;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float y = (texture(u_y, v_uv).r - 16.0 / 255.0) * (255.0 / 219.0);\n"
    "    vec2 c = (texture(u_uv, v_uv).rg - 128.0 / 255.0) * (255.0 / 224.0);\n"
    "    vec3 rgb;\n"
    "    if (u_bt709 == 1)\n"
    "        rgb = vec3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x);\n"
    "    else\n"
    "        rgb = vec3(y + 1.402 * c.y, y - 0.3441 * c.x - 0.7141 * c.y, y + 1.772 * c.x);\n"
    "    o_color = vec4(clamp(rgb, 0.0, 1.0), v_color.a);\n"
    "}\n";

static GLuint linkProgram(const char *name, GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        logf("render: %s program failed to link: %s", name, log);
    }
    return p;
}

static GLuint compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        logf("render: shader error: %s", log);
    }
    return s;
}

bool Renderer::init()
{
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(m_display, nullptr, nullptr)) {
        logf("render: eglInitialize failed 0x%x", eglGetError());
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint config_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
    };
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(m_display, config_attrs, &config, 1, &count) || count == 0) {
        logf("render: eglChooseConfig failed 0x%x", eglGetError());
        return false;
    }
#if defined(__SWITCH2__)
    // Switch 2: draw at 1080p (the handheld screen's own resolution, and
    // 1080p on a TV) instead of the default 1280x720. The UI keeps its
    // 1280x720 layout and scales by 1.5, so text and pictures get sharper.
    // Switch 1 stays at 720p.
    if (SystemInfo::get().platform == "Ounce") {
        Result rc = nwindowSetModeDimensions(nwindowGetDefault(), 1920, 1080, 1920, 1080);
        if (R_FAILED(rc)) {
            logf("render: nwindowSetModeDimensions failed 0x%x, trying a fixed size", rc);
            rc = nwindowSetDimensions(nwindowGetDefault(), 1920, 1080);
            if (R_FAILED(rc))
                logf("render: nwindowSetDimensions failed 0x%x, staying at the default size", rc);
        }
    }
#endif
    m_surface = eglCreateWindowSurface(m_display, config, nwindowGetDefault(), nullptr);
    if (m_surface == EGL_NO_SURFACE) {
        logf("render: eglCreateWindowSurface failed 0x%x", eglGetError());
        return false;
    }
    const EGLint ctx_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (m_context == EGL_NO_CONTEXT) {
        logf("render: eglCreateContext failed 0x%x", eglGetError());
        return false;
    }
    eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    eglSwapInterval(m_display, 1);
    eglQuerySurface(m_display, m_surface, EGL_WIDTH, &m_surfaceW);
    eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &m_surfaceH);
    m_scale = (float)m_surfaceH / kHeight;
    logf("render: %s, surface %dx%d", (const char *)glGetString(GL_RENDERER), m_surfaceW,
         m_surfaceH);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentSource);
    m_program = linkProgram("text", vs, fs);
    m_uScreen = glGetUniformLocation(m_program, "u_screen");

    vs = compileShader(GL_VERTEX_SHADER, kVertexSource);
    fs = compileShader(GL_FRAGMENT_SHADER, kImageFragmentSource);
    m_imageProgram = linkProgram("image", vs, fs);
    m_uImgScreen = glGetUniformLocation(m_imageProgram, "u_screen");
    m_uImgRect = glGetUniformLocation(m_imageProgram, "u_rect");
    m_uImgShape = glGetUniformLocation(m_imageProgram, "u_shape");
    m_uImgRadius = glGetUniformLocation(m_imageProgram, "u_radius");

    vs = compileShader(GL_VERTEX_SHADER, kVertexSource);
    fs = compileShader(GL_FRAGMENT_SHADER, kNv12FragmentSource);
    m_nv12Program = linkProgram("nv12", vs, fs);
    m_uNvScreen = glGetUniformLocation(m_nv12Program, "u_screen");
    m_uNvBt709 = glGetUniformLocation(m_nv12Program, "u_bt709");
    glUseProgram(m_nv12Program);
    glUniform1i(glGetUniformLocation(m_nv12Program, "u_y"), 0);
    glUniform1i(glGetUniformLocation(m_nv12Program, "u_uv"), 1);

    // 1x1 white texture, for filling shapes with the image program.
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &m_whiteTex);
    glBindTexture(GL_TEXTURE_2D, m_whiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, r));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glGenTextures(1, &m_atlasTex);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_atlas.assign(kAtlasSize * kAtlasSize, 0);
    resetAtlas();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE,
                 m_atlas.data());
    m_atlasDirty = false;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    return initFonts();
}

bool Renderer::initFonts()
{
    if (FT_Init_FreeType(&m_ft) != 0) {
        logf("render: FT_Init_FreeType failed");
        return false;
    }
    // An optional user font on the SD card comes first (for example one with
    // wider Unicode coverage), then the console's shared system fonts.
    const char *user_fonts[] = {"sdmc:/switch/OunceCord/font.ttf", "sdmc:/switch/OunceCord/font.otf"};
    for (const char *path : user_fonts) {
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> data(size);
        if (fread(data.data(), 1, size, f) == (size_t)size) {
            m_fontData.push_back(std::move(data));
            FT_Face face;
            if (FT_New_Memory_Face(m_ft, m_fontData.back().data(), size, 0, &face) == 0) {
                m_faces.push_back(face);
                logf("render: loaded %s", path);
            }
        }
        fclose(f);
    }

    // The built-in Open Sans next (when there's no user font, also its
    // semibold for names and headings), then the built-in backup fonts for
    // symbols and "fancy" letters, then the system fonts for the characters
    // those lack (Japanese, Chinese, Korean). Emoji are drawn as pictures.
    std::string_view normal = assets::get("font/normal");
    bool user_font = !m_faces.empty();
    FT_Face face;
    if (!normal.empty() && loadMemoryFace(normal.data(), normal.size(), face))
        m_faces.push_back(face);
    for (const char *name : {"font/fallback/symbols", "font/fallback/math", "font/fallback/enclosed"}) {
        std::string_view data = assets::get(name);
        if (!data.empty() && loadMemoryFace(data.data(), data.size(), face))
            m_faces.push_back(face);
    }
    const char *styled[4] = {nullptr, "font/semibold", "font/italic", "font/semibold-italic"};
    for (int k = 1; k < 4 && !user_font; k++) {
        std::string_view data = assets::get(styled[k]);
        if (!data.empty() && loadMemoryFace(data.data(), data.size(), face))
            m_styleFaces[k] = face;
    }

    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        const PlSharedFontType order[] = {
            PlSharedFontType_Standard, PlSharedFontType_NintendoExt,
            PlSharedFontType_ChineseSimplified, PlSharedFontType_ExtChineseSimplified,
            PlSharedFontType_ChineseTraditional, PlSharedFontType_KO,
        };
        for (PlSharedFontType type : order) {
            PlFontData font;
            if (R_FAILED(plGetSharedFontByType(&font, type)))
                continue;
            if (FT_New_Memory_Face(m_ft, (const FT_Byte *)font.address, font.size, 0, &face) == 0)
                m_faces.push_back(face);
        }
    } else {
        logf("render: plInitialize failed");
    }

    if (m_faces.empty()) {
        logf("render: no fonts available");
        return false;
    }
    return true;
}

bool Renderer::loadMemoryFace(const void *data, size_t size, FT_Face &face)
{
    return FT_New_Memory_Face(m_ft, (const FT_Byte *)data, (FT_Long)size, 0, &face) == 0;
}

void Renderer::exit()
{
    for (FT_Face f : m_faces)
        FT_Done_Face(f);
    m_faces.clear();
    for (FT_Face &f : m_styleFaces) {
        if (f)
            FT_Done_Face(f);
        f = nullptr;
    }
    if (m_ft)
        FT_Done_FreeType(m_ft);
    m_ft = nullptr;
    plExit();

    if (m_display != EGL_NO_DISPLAY) {
        glDeleteTextures(1, &m_atlasTex);
        glDeleteBuffers(1, &m_vbo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteProgram(m_program);
        glDeleteProgram(m_imageProgram);
        glDeleteTextures(1, &m_whiteTex);
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_context != EGL_NO_CONTEXT)
            eglDestroyContext(m_display, m_context);
        if (m_surface != EGL_NO_SURFACE)
            eglDestroySurface(m_display, m_surface);
        eglTerminate(m_display);
    }
    m_display = EGL_NO_DISPLAY;
}

void Renderer::resetAtlas()
{
    std::fill(m_atlas.begin(), m_atlas.end(), 0);
    // 4x4 white block for solid fills; sample its centre.
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            m_atlas[y * kAtlasSize + x] = 255;
    m_penX = 6;
    m_penY = 0;
    m_rowH = 6;
    m_glyphs.clear();
    m_atlasDirty = true;
}

const Renderer::Glyph *Renderer::glyph(uint32_t cp, int px)
{
    FT_Face styled = m_styleFaces[m_style];
    uint64_t key = ((uint64_t)(styled ? m_style : 0) << 61) | ((uint64_t)px << 32) | cp;
    auto it = m_glyphs.find(key);
    if (it != m_glyphs.end())
        return &it->second;
    if (cp >= kInlineFirst && cp <= kInlineLast) {
        Glyph g{0, 0, 0, 0, 0, 0, 0, 0, roundf(px * kInlineAdvance)};
        return &m_glyphs.emplace(key, g).first->second;
    }
    if (cp >= kMarkFirst && cp <= kMarkLast) {
        Glyph g{0, 0, 0, 0, 0, 0, 0, 0, 0};
        return &m_glyphs.emplace(key, g).first->second;
    }

    FT_Face face = nullptr;
    FT_UInt index = 0;
    if (styled && (index = FT_Get_Char_Index(styled, cp)))
        face = styled;
    for (FT_Face f : m_faces) {
        if (face)
            break;
        index = FT_Get_Char_Index(f, cp);
        if (index) {
            face = f;
            break;
        }
    }
    if (!face) {
        if (cp == 0xFFFD || cp == '?')
            return nullptr;
        return glyph(cp == ' ' ? '?' : 0xFFFD, px);
    }
    FT_Set_Pixel_Sizes(face, 0, px);
    if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0)
        return nullptr;
    FT_GlyphSlot slot = face->glyph;
    int w = slot->bitmap.width, h = slot->bitmap.rows;

    if (m_penX + w + 1 >= kAtlasSize) {
        m_penX = 0;
        m_penY += m_rowH + 1;
        m_rowH = 0;
    }
    if (m_penY + h + 1 >= kAtlasSize) {
        // Atlas full: start over. Text drawn earlier this frame stays valid
        // because we flush first.
        flush();
        resetAtlas();
        return glyph(cp, px);
    }
    for (int y = 0; y < h; y++)
        memcpy(&m_atlas[(m_penY + y) * kAtlasSize + m_penX],
               slot->bitmap.buffer + y * slot->bitmap.pitch, w);

    Glyph g;
    g.u0 = (float)m_penX / kAtlasSize;
    g.v0 = (float)m_penY / kAtlasSize;
    g.u1 = (float)(m_penX + w) / kAtlasSize;
    g.v1 = (float)(m_penY + h) / kAtlasSize;
    g.w = w;
    g.h = h;
    g.left = slot->bitmap_left;
    g.top = slot->bitmap_top;
    g.advance = slot->advance.x / 64.0f;
    m_penX += w + 1;
    if (h > m_rowH)
        m_rowH = h;
    m_atlasDirty = true;
    return &(m_glyphs[key] = g);
}

void Renderer::beginFrame(Color clear)
{
    glViewport(0, 0, m_surfaceW, m_surfaceH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_clips.clear();
    m_verts.clear();
}

void Renderer::endFrame()
{
    flush();
    eglSwapBuffers(m_display, m_surface);
}

void Renderer::flush()
{
    if (m_verts.empty())
        return;
    glUseProgram(m_program);
    glUniform2f(m_uScreen, (float)m_surfaceW, (float)m_surfaceH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    if (m_atlasDirty) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasSize, kAtlasSize, GL_RED, GL_UNSIGNED_BYTE,
                        m_atlas.data());
        m_atlasDirty = false;
    }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_verts.size() * sizeof(Vertex), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_verts.size());
    m_verts.clear();
}

void Renderer::pushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                    float v1, Color c)
{
    Vertex a{x0, y0, u0, v0, c.r, c.g, c.b, c.a};
    Vertex b{x1, y0, u1, v0, c.r, c.g, c.b, c.a};
    Vertex d{x0, y1, u0, v1, c.r, c.g, c.b, c.a};
    Vertex e{x1, y1, u1, v1, c.r, c.g, c.b, c.a};
    m_verts.push_back(a);
    m_verts.push_back(b);
    m_verts.push_back(d);
    m_verts.push_back(b);
    m_verts.push_back(e);
    m_verts.push_back(d);
}

static constexpr float kWhiteU = 2.0f / 2048.0f;

void Renderer::fillRect(Rect r, Color c)
{
    float s = m_scale;
    pushQuad(r.x * s, r.y * s, (r.x + r.w) * s, (r.y + r.h) * s, kWhiteU, kWhiteU, kWhiteU, kWhiteU, c);
}

void Renderer::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c)
{
    float s = m_scale;
    m_verts.push_back({x0 * s, y0 * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a});
    m_verts.push_back({x1 * s, y1 * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a});
    m_verts.push_back({x2 * s, y2 * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a});
}

void Renderer::fillCircle(float cx, float cy, float radius, Color c)
{
    float s = m_scale;
    // Enough segments that the edge doesn't look faceted at this size.
    const int segments = std::clamp((int)(radius * s * 1.6f), 24, 128);
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * 6.2831853f;
        float a1 = (float)(i + 1) / segments * 6.2831853f;
        Vertex center{cx * s, cy * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
        Vertex p0{(cx + cosf(a0) * radius) * s, (cy + sinf(a0) * radius) * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
        Vertex p1{(cx + cosf(a1) * radius) * s, (cy + sinf(a1) * radius) * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
        m_verts.push_back(center);
        m_verts.push_back(p0);
        m_verts.push_back(p1);
    }
}

void Renderer::fillRoundRect(Rect r, float radius, Color c)
{
    if (radius * 2 > r.w)
        radius = r.w / 2;
    if (radius * 2 > r.h)
        radius = r.h / 2;
    fillRect({r.x + radius, r.y, r.w - 2 * radius, r.h}, c);
    fillRect({r.x, r.y + radius, radius, r.h - 2 * radius}, c);
    fillRect({r.x + r.w - radius, r.y + radius, radius, r.h - 2 * radius}, c);
    // Corner quarter-circles, drawn as fans.
    float s = m_scale;
    const float cxs[4] = {r.x + radius, r.x + r.w - radius, r.x + r.w - radius, r.x + radius};
    const float cys[4] = {r.y + radius, r.y + radius, r.y + r.h - radius, r.y + r.h - radius};
    const int segments = 6;
    for (int k = 0; k < 4; k++) {
        float start = 3.14159265f + k * 1.5707963f;
        for (int i = 0; i < segments; i++) {
            float a0 = start + (float)i / segments * 1.5707963f;
            float a1 = start + (float)(i + 1) / segments * 1.5707963f;
            Vertex center{cxs[k] * s, cys[k] * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
            Vertex p0{(cxs[k] + cosf(a0) * radius) * s, (cys[k] + sinf(a0) * radius) * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
            Vertex p1{(cxs[k] + cosf(a1) * radius) * s, (cys[k] + sinf(a1) * radius) * s, kWhiteU, kWhiteU, c.r, c.g, c.b, c.a};
            m_verts.push_back(center);
            m_verts.push_back(p0);
            m_verts.push_back(p1);
        }
    }
}

float Renderer::drawText(float x, float y, std::string_view text, float size, Color c,
                         float max_width)
{
    int px = (int)lroundf(size * m_scale);
    float pen = x * m_scale;
    float base = (y + ascent(size)) * m_scale;
    float limit = max_width > 0 ? (x + max_width) * m_scale : 1e9f;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = utf8Next(text, i);
        if (cp == '\n' || cp == '\r')
            continue;
        if (cp == '\t')
            cp = ' ';
        const Glyph *g = glyph(cp, px);
        if (!g)
            continue;
        if (pen + g->advance > limit)
            break;
        if (g->w && g->h) {
            float gx = roundf(pen + g->left);
            float gy = roundf(base - g->top);
            pushQuad(gx, gy, gx + g->w, gy + g->h, g->u0, g->v0, g->u1, g->v1, c);
        }
        pen += g->advance;
    }
    return pen / m_scale - x;
}

float Renderer::measureText(std::string_view text, float size)
{
    int px = (int)lroundf(size * m_scale);
    float pen = 0;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = utf8Next(text, i);
        if (cp == '\n' || cp == '\r')
            continue;
        if (cp == '\t')
            cp = ' ';
        if (const Glyph *g = glyph(cp, px))
            pen += g->advance;
    }
    return pen / m_scale;
}

std::vector<std::string> Renderer::wrapText(std::string_view text, float size, float width)
{
    std::vector<std::string> lines;
    int px = (int)lroundf(size * m_scale);
    float limit = width * m_scale;

    size_t para_start = 0;
    while (para_start <= text.size()) {
        size_t nl = text.find('\n', para_start);
        std::string_view para = text.substr(para_start, nl == std::string_view::npos ? std::string_view::npos : nl - para_start);

        // Greedy wrap: break at the last space that fits, or mid-word when a
        // single word is wider than the line.
        size_t line_start = 0, last_space = std::string_view::npos;
        float pen = 0, pen_at_space = 0;
        size_t i = 0;
        while (i < para.size()) {
            size_t cp_start = i;
            uint32_t cp = utf8Next(para, i);
            const Glyph *g = glyph(cp == '\t' ? ' ' : cp, px);
            float adv = g ? g->advance : 0;
            if (cp == ' ') {
                last_space = cp_start;
                pen_at_space = pen + adv;
            }
            if (pen + adv > limit && cp_start > line_start) {
                if (last_space != std::string_view::npos && last_space > line_start) {
                    lines.emplace_back(para.substr(line_start, last_space - line_start));
                    line_start = last_space + 1;
                    pen = pen - pen_at_space;
                } else {
                    lines.emplace_back(para.substr(line_start, cp_start - line_start));
                    line_start = cp_start;
                    pen = 0;
                }
                last_space = std::string_view::npos;
            }
            pen += adv;
        }
        lines.emplace_back(para.substr(line_start));
        if (nl == std::string_view::npos)
            break;
        para_start = nl + 1;
    }
    return lines;
}

std::string Renderer::ellipsize(std::string_view text, float size, float width)
{
    if (measureText(text, size) <= width)
        return std::string(text);
    const char *dots = "...";
    float avail = width - measureText(dots, size);
    int px = (int)lroundf(size * m_scale);
    float pen = 0;
    size_t i = 0, cut = 0;
    while (i < text.size()) {
        size_t start = i;
        uint32_t cp = utf8Next(text, i);
        const Glyph *g = glyph(cp, px);
        pen += (g ? g->advance : 0) / m_scale;
        if (pen > avail)
            break;
        cut = i;
        (void)start;
    }
    return std::string(text.substr(0, cut)) + dots;
}

void Renderer::fillShape(Rect r, Shape shape, Color c)
{
    drawImage(m_whiteTex, r, shape, c);
}

void Renderer::drawImage(GLuint tex, Rect r, Shape shape, Color tint)
{
    if (!tex)
        return;
    flush();  // images use their own program and texture
    float s = m_scale;
    pushQuad(r.x * s, r.y * s, (r.x + r.w) * s, (r.y + r.h) * s, 0, 0, 1, 1, tint);
    glUseProgram(m_imageProgram);
    glUniform2f(m_uImgScreen, (float)m_surfaceW, (float)m_surfaceH);
    glUniform4f(m_uImgRect, r.x * s, r.y * s, r.w * s, r.h * s);
    glUniform1i(m_uImgShape, shape == Shape::Circle ? 0 : shape == Shape::Squircle ? 1 : 2);
    glUniform1f(m_uImgRadius, shape == Shape::Rect ? 0.0f : 6.0f * s);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_verts.size() * sizeof(Vertex), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_verts.size());
    m_verts.clear();
}

void Renderer::drawNv12(GLuint y_tex, GLuint uv_tex, Rect r, bool bt709)
{
    if (!y_tex || !uv_tex)
        return;
    flush();
    float s = m_scale;
    pushQuad(r.x * s, r.y * s, (r.x + r.w) * s, (r.y + r.h) * s, 0, 0, 1, 1, Color(255, 255, 255));
    glUseProgram(m_nv12Program);
    glUniform2f(m_uNvScreen, (float)m_surfaceW, (float)m_surfaceH);
    glUniform1i(m_uNvBt709, bt709 ? 1 : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, uv_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, y_tex);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_verts.size() * sizeof(Vertex), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_verts.size());
    m_verts.clear();
}

void Renderer::applyClip()
{
    if (m_clips.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    Rect r = m_clips.back();
    glEnable(GL_SCISSOR_TEST);
    int x = (int)floorf(r.x * m_scale);
    int y = (int)floorf(r.y * m_scale);
    int w = (int)ceilf(r.w * m_scale);
    int h = (int)ceilf(r.h * m_scale);
    // GL's origin is the bottom-left corner.
    glScissor(x, m_surfaceH - (y + h), w < 0 ? 0 : w, h < 0 ? 0 : h);
}

void Renderer::pushClip(Rect r)
{
    flush();
    if (!m_clips.empty()) {
        Rect p = m_clips.back();
        float x0 = std::max(p.x, r.x), y0 = std::max(p.y, r.y);
        float x1 = std::min(p.x + p.w, r.x + r.w), y1 = std::min(p.y + p.h, r.y + r.h);
        r = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    m_clips.push_back(r);
    applyClip();
}

void Renderer::popClip()
{
    flush();
    if (!m_clips.empty())
        m_clips.pop_back();
    applyClip();
}
