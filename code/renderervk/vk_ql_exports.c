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

The four text entries are wired to tr_font_vk.c, the Vulkan backend for the
fontstash atlas.
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

// tr_font_vk.c
void RE_Font_DrawString(int x, int y, const char *text, int fontIndex, float scale, int limit, float *maxX, int flags);
void RE_TextBounds(const char *text, int start, int limit, float scale, int fontIndex, int *outX, int *outY, int *outW, int *outH);
void RE_GetGlyphInfo(int fontIndex, int charValue, glyphInfo_t *glyph);
void RE_SetCompositionFont(int fontIndex, float scale);

/*
===============
VK_Get_Advertisements

In-map advertisement surfaces. renderergl2's RE_Get_Advertisements reports none
either - the feature needs the ad server this build does not talk to - so this
is the same answer, not a Vulkan shortcoming.
===============
*/
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
    rexp->Font_DrawString = RE_Font_DrawString;
    rexp->TextBounds = RE_TextBounds;
    rexp->GetGlyphInfo = RE_GetGlyphInfo;
    rexp->SetCompositionFont = RE_SetCompositionFont;
    rexp->Get_Advertisements = VK_Get_Advertisements;
}
