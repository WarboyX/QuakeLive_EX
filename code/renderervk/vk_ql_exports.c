/*
===========================================================================
vk_ql_exports.c - the two places this tree's module interface differs from
Quake3e's, kept out of the vendored source.

renderervk/tr_init.c's GetRefAPI fills Quake3e's refimport_t / refexport_t.
This tree's are not the same shape, in two directions:

  imports  Quake3e's engine owns the window and supplies VKimp_Init,
           VKimp_Shutdown, VK_CreateSurface, VK_GetInstanceProcAddr and the
           gamma pair. Here the renderer owns the window, so the engine leaves
           those null and this module fills them in from vk_window.c.

  exports  Quake Live's refexport_t has five entries Quake3e has never heard
           of - four for the TrueType text system and one for in-map
           advertisements. Left null the engine calls through a null pointer
           the first time anything draws text, which is immediately.

GetRefAPI calls VK_QL_FillImports and VK_QL_FillExports; that is the whole
patch to the vendored file, and it is two lines.

The font entries here are stubs and this build cannot draw text. That is a
statement of the current state, not a design: the console is the debugging
surface for a renderer, so a Vulkan build that cannot print is a Vulkan build
that cannot be debugged from inside itself. Porting tr_font_gl.c's atlas to a
vk image is the next piece of work, not polish afterwards.
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"

void VKimp_Init(glconfig_t *config);
void VKimp_Shutdown(qboolean unloadDLL);
void VKimp_InitGamma(glconfig_t *config);
qboolean VKimp_CreateSurface(void *instance, void **surface);
void *VKimp_GetInstanceProcAddr(void *instance, const char *name);

// sdl_gamma.c, built into this module as it is into renderergl2
void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256]);

static void VK_TextNotImplemented(void) {
    static qboolean reported;

    if (!reported) {
        reported = qtrue;
        ri.Printf(PRINT_ALL,
                  "^3WARNING:^7 the Vulkan renderer cannot draw text yet - the console and "
                  "every menu will be blank. Use cl_renderer opengl2 to get it back.\n");
    }
}

/*
===============
RE_RegisterFont

A no-op in Quake Live too: fonts are loaded inside the renderer, and this
entry point survives only because the UI still calls it. renderergl2's is the
same stub, so this is not a Vulkan shortcoming.
===============
*/
void RE_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
    if (font) {
        Com_Memset(font, 0, sizeof(*font));
    }
}

void R_InitFreeType(void) {}

void R_DoneFreeType(void) {}

static void VK_Font_DrawString(int x, int y, const char *text, int fontIndex, float scale, int limit, float *maxX, int flags) {
    VK_TextNotImplemented();

    if (maxX) {
        *maxX = (float)x;
    }
}

static void VK_TextBounds(const char *text, int start, int limit, float scale, int fontIndex, int *outX, int *outY, int *outW, int *outH) {
    VK_TextNotImplemented();

    if (outX) *outX = 0;
    if (outY) *outY = 0;
    if (outW) *outW = 0;
    if (outH) *outH = 0;
}

static void VK_GetGlyphInfo(int fontIndex, int charValue, glyphInfo_t *glyph) {
    VK_TextNotImplemented();

    if (glyph) {
        Com_Memset(glyph, 0, sizeof(*glyph));
    }
}

static void VK_SetCompositionFont(int fontIndex, float scale) {}

static void VK_Get_Advertisements(int *num, float *verts, char shaders[][MAX_QPATH]) {
    if (num) {
        *num = 0;
    }
}

void VK_QL_FillImports(refimport_t *rimp) {
    rimp->VKimp_Init = VKimp_Init;
    rimp->VKimp_Shutdown = VKimp_Shutdown;
    rimp->VK_CreateSurface = VKimp_CreateSurface;
    rimp->VK_GetInstanceProcAddr = VKimp_GetInstanceProcAddr;

    // Quake3e's names; both are SDL window calls with nothing GL about them.
    rimp->GLimp_InitGamma = VKimp_InitGamma;
    rimp->GLimp_SetGamma = GLimp_SetGamma;
}

void VK_QL_FillExports(refexport_t *rexp) {
    rexp->Font_DrawString = VK_Font_DrawString;
    rexp->TextBounds = VK_TextBounds;
    rexp->GetGlyphInfo = VK_GetGlyphInfo;
    rexp->SetCompositionFont = VK_SetCompositionFont;
    rexp->Get_Advertisements = VK_Get_Advertisements;
}
