/*
===========================================================================
tr_font_vk.c - Quake Live font subsystem, Vulkan backend.

The Vulkan half of what renderergl2/tr_font_gl.c does for OpenGL: the five
fontstash render callbacks, plus the engine-facing entry points reached through
the re. refexport table.

Only the callbacks are backend-specific, and they differ in one respect.
renderergl2 keeps the atlas as a single-channel GL_R8 texture and reads it as
coverage through a texture swizzle (1,1,1,R), reproducing Quake Live's legacy
GL_ALPHA atlas. Vulkan has no swizzle to lean on here without a second image
view and a dedicated descriptor, so this expands each dirty rect to RGBA on the
way in - white with coverage in alpha, which is exactly what that swizzle
produces. It costs 4x the atlas memory (8MB at the 2048x1024 cap) and one pass
over the dirty pixels per update, and it needs no format tricks at all.

Everything from R_Font_HandleError down is a copy of tr_font_gl.c and is
backend-agnostic: fontstash state, TTF loading, the index table, and the five
RE_ entry points. It is duplicated rather than shared because factoring it out
means editing the one renderer that currently works, and the safe order is to
prove this one first. The two must be kept in step until then - the shared part
starts at the "atlas-full error handler" banner in both files.
===========================================================================
*/

#include "tr_local.h"
#include "../renderercommon/tr_fontstash.h"
#include "../renderercommon/tr_stbtt.h"

// Current 2D draw colour, shadowed from RE_SetColor (see tr_cmds.c). Quake Live
// sets text colour with trap_R_SetColor before trap_R_Font_DrawString; it
// becomes the fontstash base colour, so glyphs inherit it and '^' colour codes
// layer over it.
vec4_t g_fontColor2D = {1.0f, 1.0f, 1.0f, 1.0f};

#define FONT_ATLAS_INIT 512
#define FONT_ATLAS_MAX_W 2048
#define FONT_ATLAS_MAX_H 1024

static FONScontext *fontContext = NULL;
static image_t *fontImage = NULL;
static qhandle_t fontShader = 0;
static int fontAtlasWidth = 0;
static int fontAtlasHeight = 0;

// Scratch for the coverage -> RGBA expansion in renderUpdate. Grown on demand
// rather than sized for the whole atlas: the dirty rect is usually one glyph.
static byte *fontStaging = NULL;
static int fontStagingSize = 0;

// index (FONT_DEFAULT=0, FONT_SANS=1, FONT_MONO=2) -> fontstash font id
static int fontIndexToId[8];
static int numFontIndices = 0;

// IME composition font state (recorded for the IME candidate window).
static int imeCompositionFont = 0;
static float imeCompositionScale = 0.0f;

// -------------------------------------------------------------------------
//  colour helpers
// -------------------------------------------------------------------------

static unsigned int R_Font_PackColor(const vec4_t c) {
    int r = (int)(c[0] * 255.0f + 0.5f);
    int g = (int)(c[1] * 255.0f + 0.5f);
    int b = (int)(c[2] * 255.0f + 0.5f);
    int a = (int)(c[3] * 255.0f + 0.5f);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    return ((unsigned int)r) | ((unsigned int)g << 8) | ((unsigned int)b << 16) | ((unsigned int)a << 24);
}

// -------------------------------------------------------------------------
//  fontstash Vulkan backend callbacks (== QL nvgl__render*)
// -------------------------------------------------------------------------

static qboolean R_Font_ReserveStaging(int bytes) {
    if (bytes <= fontStagingSize) {
        return qtrue;
    }

    free(fontStaging);
    fontStaging = (byte *)malloc(bytes);
    fontStagingSize = fontStaging ? bytes : 0;

    return fontStaging != NULL;
}

/*
Expand w*h single-byte coverage into RGBA. src is a window into the full atlas,
so it is walked with the atlas pitch rather than the rect width - fontstash
hands over a pointer to the whole atlas and the rect that changed in it.
*/
static void R_Font_ExpandCoverage(byte *dst, const byte *src, int srcPitch, int x, int y, int w, int h) {
    int row, col;

    for (row = 0; row < h; row++) {
        const byte *s = src + (size_t)(y + row) * srcPitch + x;
        byte *d = dst + (size_t)row * w * 4;

        for (col = 0; col < w; col++) {
            *d++ = 255;
            *d++ = 255;
            *d++ = 255;
            *d++ = *s++;
        }
    }
}

static int RB_Font_renderCreate(void *uptr, int width, int height) {
    byte *staging;

    (void)uptr;

    // A zeroed RGBA atlas: fully transparent, so nothing shows until glyphs are
    // rasterised into it.
    staging = (byte *)calloc((size_t)width * height, 4);
    if (staging == NULL) {
        return 0;
    }

    // No mipmaps (2D text is drawn at its own scale), no compression (a block
    // format would smear the coverage), no light scaling, clamped so a glyph at
    // the atlas edge cannot bleed round to the other side.
    fontImage = R_CreateImage("*fontstash", NULL, staging, width, height,
                              IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NOSCALE);
    free(staging);

    if (fontImage == NULL) {
        return 0;
    }

    fontShader = RE_RegisterShaderFromImage("*fontstash", LIGHTMAP_2D, fontImage, qfalse);

    fontAtlasWidth = width;
    fontAtlasHeight = height;

    return 1;
}

static int RB_Font_renderResize(void *uptr, int width, int height) {
    // Unlike the GL path there is no "redefine storage on the same texture
    // object" here - a VkImage's extent is fixed at creation. Creating a new
    // one leaks the old image_t until vid_restart, which is why the atlas is
    // capped at 2048x1024 and reset rather than grown past it (see
    // R_Font_HandleError): the number of resizes over a session is bounded at
    // three.
    return RB_Font_renderCreate(uptr, width, height);
}

static void RB_Font_renderUpdate(void *uptr, int *rect, const unsigned char *data) {
    int x = rect[0], y = rect[1];
    int w = rect[2] - rect[0];
    int h = rect[3] - rect[1];
    int bytes;

    (void)uptr;

    if (fontImage == NULL || w <= 0 || h <= 0) {
        return;
    }

    bytes = w * h * 4;
    if (!R_Font_ReserveStaging(bytes)) {
        return;
    }

    R_Font_ExpandCoverage(fontStaging, data, fontAtlasWidth, x, y, w, h);

    vk_upload_image_data(fontImage, x, y, w, h, 1, fontStaging, bytes, qtrue);
}

static void RB_Font_renderDraw(void *uptr, const float *verts, const float *tcoords, const unsigned int *colors, int nverts) {
    int i;
    unsigned int lastColor = 0;
    qboolean haveColor = qfalse;

    (void)uptr;

    if (fontShader == 0) {
        return;
    }

    // Each glyph is 6 verts (2 triangles). Vertex i is the top-left corner
    // (x0,y0,s0,t0) and vertex i+1 the bottom-right (x1,y1,s1,t1); re-emit as a
    // single StretchPic quad, matching QL's nvgl__renderDraw -> DrawStretchRaw.
    for (i = 0; i + 6 <= nverts; i += 6) {
        unsigned int c = colors[i];
        float x0 = verts[i * 2 + 0];
        float y0 = verts[i * 2 + 1];
        float x1 = verts[(i + 1) * 2 + 0];
        float y1 = verts[(i + 1) * 2 + 1];
        float s0 = tcoords[i * 2 + 0];
        float t0 = tcoords[i * 2 + 1];
        float s1 = tcoords[(i + 1) * 2 + 0];
        float t1 = tcoords[(i + 1) * 2 + 1];

        if (!haveColor || c != lastColor) {
            vec4_t rgba;
            rgba[0] = (float)(c & 0xff) / 255.0f;
            rgba[1] = (float)((c >> 8) & 0xff) / 255.0f;
            rgba[2] = (float)((c >> 16) & 0xff) / 255.0f;
            rgba[3] = (float)((c >> 24) & 0xff) / 255.0f;
            RE_SetColor(rgba);
            lastColor = c;
            haveColor = qtrue;
        }

        RE_StretchPic(x0, y0, x1 - x0, y1 - y0, s0, t0, s1, t1, fontShader);
    }

    // Restore the default 2D colour so subsequent StretchPics are unaffected.
    if (haveColor) {
        RE_SetColor(NULL);
    }
}

static void RB_Font_renderDelete(void *uptr) {
    (void)uptr;

    // The image_t / shader are owned by the renderer's media manager and are
    // released on R_DeleteTextures / vid_restart; nothing to free here.
    fontImage = NULL;
    fontShader = 0;
    fontAtlasWidth = 0;
    fontAtlasHeight = 0;

    free(fontStaging);
    fontStaging = NULL;
    fontStagingSize = 0;
}

// -------------------------------------------------------------------------
//  atlas-full error handler (== QL R_ExpandFontAtlas)
// -------------------------------------------------------------------------

static void R_Font_HandleError(void *uptr, int error, int val) {
    FONScontext *ctx = (FONScontext *)uptr;
    int w, h;

    (void)val;

    if (error != FONS_ATLAS_FULL)
        return;

    fonsGetAtlasSize(ctx, &w, &h);

    // Grow by doubling, capped at 2048x1024 (QL's cap); at the cap, reset the
    // atlas instead of growing further.
    if (w <= h && w < FONT_ATLAS_MAX_W)
        w *= 2;
    else if (h < FONT_ATLAS_MAX_H)
        h *= 2;
    else if (w < FONT_ATLAS_MAX_W)
        w *= 2;
    else {
        fonsResetAtlas(ctx, FONT_ATLAS_MAX_W, FONT_ATLAS_MAX_H);
        return;
    }

    fonsExpandAtlas(ctx, w, h);
}

// -------------------------------------------------------------------------
//  font loading and lifecycle
// -------------------------------------------------------------------------

static int R_Font_LoadTTF(const char *path, const char *name) {
    void *buffer = NULL;
    unsigned char *copy;
    int len, id;

    len = ri.FS_ReadFile(path, &buffer);
    if (len <= 0 || buffer == NULL) {
        if (buffer)
            ri.FS_FreeFile(buffer);
        return FONS_INVALID;
    }

    // fontstash keeps the font blob for the life of the context; give it its own
    // copy (freeData=1) and release the FS buffer.
    copy = (unsigned char *)malloc(len);
    if (copy == NULL) {
        ri.FS_FreeFile(buffer);
        return FONS_INVALID;
    }
    Com_Memcpy(copy, buffer, len);
    ri.FS_FreeFile(buffer);

    id = fonsAddFont(fontContext, name, copy, len, 1);
    if (id == FONS_INVALID) {
        free(copy);
        ri.Printf(PRINT_WARNING, "R_InitFonts: failed to parse font '%s'\n", path);
    }
    return id;
}

void R_InitFonts(void) {
    FONSparams params;
    int normal, sans, mono, fallback;

    if (fontContext != NULL)
        return;

    Com_Memset(&params, 0, sizeof(params));
    params.width = FONT_ATLAS_INIT;
    params.height = FONT_ATLAS_INIT;
    params.flags = FONS_ZERO_TOPLEFT; // QL uses top-left screen coordinates
    params.userPtr = NULL;
    params.renderCreate = RB_Font_renderCreate;
    params.renderResize = RB_Font_renderResize;
    params.renderUpdate = RB_Font_renderUpdate;
    params.renderDraw = RB_Font_renderDraw;
    params.renderDelete = RB_Font_renderDelete;

    fontContext = fonsCreateInternal(&params);
    if (fontContext == NULL) {
        ri.Printf(PRINT_WARNING, "R_InitFonts: failed to create font context\n");
        return;
    }
    fonsSetErrorCallback(fontContext, R_Font_HandleError, fontContext);

    // Load the QL fonts by name.
    normal = R_Font_LoadTTF(DEFAULT_FONT, "normal");
    sans = R_Font_LoadTTF(DEFAULT_SANS_FONT, "sans");
    mono = R_Font_LoadTTF(DEFAULT_MONO_FONT, "mono");
    fallback = R_Font_LoadTTF("fonts/droidsansfallbackfull.ttf", "sans-fallback");

    // NOTE: QL also loads an OS-host Unicode fallback (ARIALUNI.TTF / segoeui.ttf
    // / l_10646.ttf) by reading the Windows fonts directory directly.  This loads
    // only from the game filesystem, so that OS-host fallback is skipped;
    // droidsansfallbackfull.ttf gives the broad Unicode/CJK coverage.

    // Font index table (menudef: FONT_DEFAULT=0, FONT_SANS=1, FONT_MONO=2).
    fontIndexToId[0] = (normal != FONS_INVALID) ? normal : 0;
    fontIndexToId[1] = (sans != FONS_INVALID) ? sans : fontIndexToId[0];
    fontIndexToId[2] = (mono != FONS_INVALID) ? mono : fontIndexToId[0];
    numFontIndices = 3;

    // Fall back to the broad-coverage font for glyphs the primaries lack.
    if (fallback != FONS_INVALID) {
        if (normal != FONS_INVALID)
            fonsAddFallbackFont(fontContext, normal, fallback);
        if (sans != FONS_INVALID)
            fonsAddFallbackFont(fontContext, sans, fallback);
        if (mono != FONS_INVALID)
            fonsAddFallbackFont(fontContext, mono, fallback);
    }

    if (normal == FONS_INVALID)
        ri.Printf(PRINT_WARNING, "R_InitFonts: default font '%s' missing - text will not render\n", DEFAULT_FONT);
}

void R_FontShutdown(void) {
    if (fontContext == NULL)
        return;
    fonsDeleteInternal(fontContext);
    fontContext = NULL;
    numFontIndices = 0;
}

/*
The vendored R_Init and RE_Shutdown call R_InitFreeType / R_DoneFreeType, from
back when Quake3e's text came from FreeType. FreeType is gone from this tree -
text is fontstash over stb_truetype - so rather than patch two more lines into
the vendored file, the old names are the hooks the font system hangs off.
*/
void R_InitFreeType(void) { R_InitFonts(); }

void R_DoneFreeType(void) { R_FontShutdown(); }

// -------------------------------------------------------------------------
//  engine entry points (reached through re.*)
// -------------------------------------------------------------------------

static int R_Font_ResolveId(int fontIndex) {
    if (fontIndex < 0 || fontIndex >= numFontIndices)
        return (numFontIndices > 0) ? fontIndexToId[0] : 0;
    return fontIndexToId[fontIndex];
}

void RE_Font_DrawString(int x, int y, const char *text, int fontIndex, float scale, int limit, float *maxX, int flags) {
    const char *end = NULL;

    if (fontContext == NULL || text == NULL || text[0] == '\0') {
        if (maxX)
            *maxX = (float)x;
        return;
    }

    fonsSetFont(fontContext, R_Font_ResolveId(fontIndex));
    fonsSetSize(fontContext, scale);
    // QL sets no alignment, so fontstash's default LEFT|BASELINE applies: the y
    // is the text baseline, matching the id Tech 3 .menu 'textaligny' convention.
    fonsSetAlign(fontContext, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);
    fonsSetSpacing(fontContext, 0.0f);
    fonsSetBlur(fontContext, 0.0f);
    fonsSetColor(fontContext, R_Font_PackColor(g_fontColor2D));

    // limit is a max character count; approximate as a byte limit (exact for
    // ASCII).
    if (limit > 0 && (int)strlen(text) > limit)
        end = text + limit;

    fonsDrawText(fontContext, (float)x, (float)y, text, end, flags, maxX);
}

void RE_TextBounds(const char *text, int start, int limit, float scale, int fontIndex, int *outX, int *outY, int *outW, int *outH) {
    float bounds[4];
    const char *str, *end;

    if (outX)
        *outX = 0;
    if (outY)
        *outY = 0;
    if (outW)
        *outW = 0;
    if (outH)
        *outH = 0;

    if (fontContext == NULL || text == NULL)
        return;

    fonsSetFont(fontContext, R_Font_ResolveId(fontIndex));
    fonsSetSize(fontContext, scale);
    fonsSetAlign(fontContext, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);
    fonsSetSpacing(fontContext, 0.0f);
    fonsSetBlur(fontContext, 0.0f);

    str = text + (start > 0 ? start : 0);
    end = (limit > 0) ? str + limit : NULL;

    fonsTextBounds(fontContext, 0.0f, 0.0f, str, end, bounds);

    // Report position and size (width/height), leaving derivation of virtual
    // 640x480 dimensions to the caller.
    if (outX)
        *outX = (int)bounds[0];
    if (outY)
        *outY = (int)bounds[1];
    if (outW)
        *outW = (int)(bounds[2] - bounds[0]);
    if (outH)
        *outH = (int)(bounds[3] - bounds[1]);
}

void RE_GetGlyphInfo(int fontIndex, int charValue, glyphInfo_t *out) {
    char buf[8];
    float bounds[4];
    float ascender, descender, lineh;
    int n = 0;
    unsigned int cp = (unsigned int)charValue;

    if (out == NULL)
        return;
    Com_Memset(out, 0, sizeof(*out));
    if (fontContext == NULL)
        return;

    // Encode the codepoint as UTF-8 into a temporary string.
    if (cp < 0x80) {
        buf[n++] = (char)cp;
    } else if (cp < 0x800) {
        buf[n++] = (char)(0xC0 | (cp >> 6));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[n++] = (char)(0xE0 | (cp >> 12));
        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[n++] = (char)(0xF0 | (cp >> 18));
        buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    }
    buf[n] = '\0';

    fonsSetFont(fontContext, R_Font_ResolveId(fontIndex));
    fonsSetAlign(fontContext, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);

    // Advance / bounds for this single glyph at the current state size.
    out->xSkip = (int)fonsTextBounds(fontContext, 0.0f, 0.0f, buf, NULL, bounds);
    out->left = (int)bounds[0];
    out->top = (int)bounds[1];
    out->imageWidth = (int)(bounds[2] - bounds[0]);
    out->imageHeight = (int)(bounds[3] - bounds[1]);
    fonsVertMetrics(fontContext, &ascender, &descender, &lineh);
    out->height = (int)(ascender - descender);
    out->glyph = fontShader;
}

void RE_SetCompositionFont(int fontIndex, float scale) {
    imeCompositionFont = fontIndex;
    imeCompositionScale = scale;
}

// Dead stub - fonts are loaded internally by R_InitFonts (matches QL, whose
// RE_RegisterFont is an empty return).  Kept to fill the legacy re.RegisterFont
// slot for any callers still holding it.
void RE_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
    (void)fontName;
    (void)pointSize;
    if (font)
        Com_Memset(font, 0, sizeof(*font));
}
