/*
===========================================================================
vk_window.c - SDL windowing for the Vulkan renderer.

Quake3e drives the window from the engine and hands the renderer four entry
points through refimport_t: VKimp_Init, VKimp_Shutdown, VK_CreateSurface and
VK_GetInstanceProcAddr. This tree does the opposite - sdl_glimp.c is compiled
*into* renderergl2, so the renderer owns its window and hands the handle back
to the client via ri.IN_Init.

Rather than restructure sdl_glimp.c - shared code the OpenGL renderer depends
on, which has to keep working throughout - this provides the same four entry
points from inside the Vulkan renderer, and GetRefAPI points ri at them after
copying the engine's import table. The vendored renderervk source is unchanged:
it still calls ri.VKimp_Init(), it just reaches code in its own module.

That leaves sdl_glimp.c untouched and out of this build entirely, which is also
why the window cvars are registered here: sdl_glimp.c took r_mode, r_fullscreen,
r_colorbits and the rest as externs defined by renderergl2/tr_init.c, and
neither file is in the Vulkan link.

Deliberately not a copy of sdl_glimp.c. There is no GL context to create, no
multisample or colour-bit fallback loop to walk (the swapchain format is chosen
by vk.c from the surface's own capabilities), and no extension string to build.
What is left is: pick a resolution, make a window with SDL_WINDOW_VULKAN, and
report it.
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#include "SDL.h"
#include "SDL_vulkan.h"
#else
#include <SDL.h>
#include <SDL_vulkan.h>
#endif

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"
#include "../sdl/sdl_icon.h"

// Both are defined by the vendored renderer: ri in tr_init.c's GetRefAPI,
// SDL_window here, because sdl_gamma.c takes it as an extern.
SDL_Window *SDL_window = NULL;

// sdl_gamma.c reads this one directly. renderervk/tr_init.c has a file-scope
// static of the same name, which is a different object and not a clash.
cvar_t *r_ignorehwgamma;

static cvar_t *r_mode;
static cvar_t *r_fullscreen;
static cvar_t *r_noborder;
static cvar_t *r_centerWindow;
static cvar_t *r_allowResize;
static cvar_t *r_customwidth;
static cvar_t *r_customheight;
static cvar_t *r_customPixelAspect;
static cvar_t *r_sdlDriver;

// The mode table is renderergl2's, which is Quake Live's own from
// quakelive_steam.exe. Both renderers have to agree with it: r_mode is archived
// and the render options menu lists modes by index, so a second table with a
// different order would silently move the user's resolution when they switch
// renderer.
typedef struct {
    int width, height;
    float pixelAspect;
} vkVidMode_t;

static const vkVidMode_t vk_vidModes[] = {
    {320, 240, 1},   {400, 300, 1},   {512, 384, 1},   {640, 360, 1},
    {640, 400, 1},   {640, 480, 1},   {800, 450, 1},   {852, 480, 1},
    {800, 500, 1},   {800, 600, 1},   {1024, 640, 1},  {1024, 576, 1},
    {1024, 768, 1},  {1152, 864, 1},  {1280, 720, 1},  {1280, 768, 1},
    {1280, 800, 1},  {1280, 1024, 1}, {1440, 900, 1},  {1600, 900, 1},
    {1600, 1000, 1}, {1680, 1050, 1}, {1600, 1200, 1}, {1920, 1080, 1},
    {1920, 1200, 1}, {1920, 1440, 1}, {2048, 1536, 1}, {2560, 1600, 1}};

#define VK_NUM_VIDMODES ARRAY_LEN(vk_vidModes)
#define VK_MODE_FALLBACK 12  // 1024x768

static qboolean VKimp_GetModeInfo(int *width, int *height, float *windowAspect, int mode) {
    float pixelAspect;

    if (mode < -1 || mode >= (int)VK_NUM_VIDMODES) {
        return qfalse;
    }

    if (mode == -1) {
        *width = r_customwidth->integer;
        *height = r_customheight->integer;
        pixelAspect = r_customPixelAspect->value;
    } else {
        *width = vk_vidModes[mode].width;
        *height = vk_vidModes[mode].height;
        pixelAspect = vk_vidModes[mode].pixelAspect;
    }

    if (*width <= 0 || *height <= 0 || pixelAspect <= 0.0f) {
        return qfalse;
    }

    *windowAspect = (float)*width / (*height * pixelAspect);

    return qtrue;
}

/*
===============
VKimp_DetectAvailableModes

Fills r_availableModes the same way sdl_glimp.c does. The render options menu's
resolution row is built from it (Item_ApplyHacks rewrites the r_mode multi from
this string), so leaving it empty falls back to a hardcoded list that may not
match the display.
===============
*/
static void VKimp_DetectAvailableModes(void) {
    char buf[MAX_STRING_CHARS] = {0};
    SDL_DisplayMode windowMode;
    int display;
    int numModes;
    int i;

    display = SDL_GetWindowDisplayIndex(SDL_window);
    if (display < 0) {
        return;
    }

    numModes = SDL_GetNumDisplayModes(display);
    if (numModes <= 0 || SDL_GetWindowDisplayMode(SDL_window, &windowMode) < 0) {
        return;
    }

    for (i = 0; i < numModes; i++) {
        SDL_DisplayMode mode;
        char modeStr[32];

        if (SDL_GetDisplayMode(display, i, &mode) < 0) {
            continue;
        }
        if (!mode.w || !mode.h) {
            continue;
        }
        if (windowMode.format != mode.format) {
            continue;
        }

        Com_sprintf(modeStr, sizeof(modeStr), "%ux%u", mode.w, mode.h);
        if (strstr(buf, modeStr)) {
            continue;  // SDL lists one entry per refresh rate
        }
        if (strlen(buf) + strlen(modeStr) + 2 >= sizeof(buf)) {
            break;
        }
        if (*buf) {
            Q_strcat(buf, sizeof(buf), " ");
        }
        Q_strcat(buf, sizeof(buf), modeStr);
    }

    if (*buf) {
        ri.Printf(PRINT_ALL, "Available modes: '%s'\n", buf);
        ri.Cvar_Set("r_availableModes", buf);
    }
}

typedef enum { VKSERR_OK, VKSERR_INVALID_MODE, VKSERR_UNKNOWN } vkserr_t;

static vkserr_t VKimp_SetMode(glconfig_t *config, int mode, qboolean fullscreen, qboolean noborder) {
    SDL_DisplayMode desktopMode;
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN;
    int display = 0;
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_Surface *icon = NULL;

    ri.Printf(PRINT_ALL, "Initializing Vulkan display\n");

    if (r_allowResize->integer) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

#ifdef USE_ICON
    icon = SDL_CreateRGBSurfaceFrom(
        (void *)CLIENT_WINDOW_ICON.pixel_data, CLIENT_WINDOW_ICON.width, CLIENT_WINDOW_ICON.height,
        CLIENT_WINDOW_ICON.bytes_per_pixel * 8, CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
        0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
    );
#endif

    if (SDL_window != NULL) {
        display = SDL_GetWindowDisplayIndex(SDL_window);
        if (display < 0) {
            display = 0;
        }
    }

    if (SDL_GetDesktopDisplayMode(display, &desktopMode) == 0) {
        ri.Printf(PRINT_ALL, "Display aspect: %.3f\n", (float)desktopMode.w / (float)desktopMode.h);
    } else {
        Com_Memset(&desktopMode, 0, sizeof(desktopMode));
        ri.Printf(PRINT_ALL, "Cannot determine display aspect, assuming 1.333\n");
    }

    ri.Printf(PRINT_ALL, "...setting mode %d:", mode);

    if (mode == -2) {
        // desktop resolution
        if (desktopMode.h > 0) {
            config->vidWidth = desktopMode.w;
            config->vidHeight = desktopMode.h;
        } else {
            config->vidWidth = 640;
            config->vidHeight = 480;
        }
        config->windowAspect = (float)config->vidWidth / (float)config->vidHeight;
    } else if (!VKimp_GetModeInfo(&config->vidWidth, &config->vidHeight, &config->windowAspect, mode)) {
        ri.Printf(PRINT_ALL, " invalid mode\n");
        if (icon) {
            SDL_FreeSurface(icon);
        }
        return VKSERR_INVALID_MODE;
    }

    ri.Printf(PRINT_ALL, " %d %d\n", config->vidWidth, config->vidHeight);

    if (r_centerWindow->integer && !fullscreen && desktopMode.h > 0) {
        x = (desktopMode.w / 2) - (config->vidWidth / 2);
        y = (desktopMode.h / 2) - (config->vidHeight / 2);
    }

    if (SDL_window != NULL) {
        SDL_GetWindowPosition(SDL_window, &x, &y);
        SDL_DestroyWindow(SDL_window);
        SDL_window = NULL;
    }

    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
        config->isFullscreen = qtrue;
    } else {
        if (noborder) {
            flags |= SDL_WINDOW_BORDERLESS;
        }
        config->isFullscreen = qfalse;
    }

    SDL_window = SDL_CreateWindow(CLIENT_WINDOW_TITLE, x, y, config->vidWidth, config->vidHeight, flags);
    if (SDL_window == NULL) {
        ri.Printf(PRINT_ALL, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        if (icon) {
            SDL_FreeSurface(icon);
        }
        return VKSERR_INVALID_MODE;
    }

    if (icon) {
        SDL_SetWindowIcon(SDL_window, icon);
        SDL_FreeSurface(icon);
    }

    if (fullscreen) {
        SDL_DisplayMode mode;

        mode.format = SDL_PIXELFORMAT_RGB24;
        mode.w = config->vidWidth;
        mode.h = config->vidHeight;
        mode.refresh_rate = config->displayFrequency = ri.Cvar_VariableIntegerValue("r_displayRefresh");
        mode.driverdata = NULL;

        if (SDL_SetWindowDisplayMode(SDL_window, &mode) < 0) {
            ri.Printf(PRINT_DEVELOPER, "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError());
        }
    }

    // The window is SDL_WINDOW_VULKAN, so SDL has the loader open already; ask
    // again anyway so a failure is reported here rather than as a null
    // vkGetInstanceProcAddr several hundred lines into vk_initialize().
    if (SDL_Vulkan_LoadLibrary(NULL) != 0) {
        ri.Printf(PRINT_ALL, "SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(SDL_window);
        SDL_window = NULL;
        return VKSERR_UNKNOWN;
    }

    // Vulkan reports the drawable in pixels, which is what the swapchain wants;
    // on a HiDPI display that is not the size asked for above.
    SDL_Vulkan_GetDrawableSize(SDL_window, &config->vidWidth, &config->vidHeight);
    config->windowAspect = (float)config->vidWidth / (float)config->vidHeight;

    VKimp_DetectAvailableModes();

    return VKSERR_OK;
}

/*
===============
VKimp_Init

ri.VKimp_Init - creates the window renderervk's vk_initialize() will draw into.
===============
*/
void VKimp_Init(glconfig_t *config) {
    int mode;

    ri.Printf(PRINT_DEVELOPER, "VKimp_Init()\n");

    r_sdlDriver = ri.Cvar_Get("r_sdlDriver", "", CVAR_ROM);
    r_allowResize = ri.Cvar_Get("r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_centerWindow = ri.Cvar_Get("r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_mode = ri.Cvar_Get("r_mode", "-2", CVAR_ARCHIVE | CVAR_LATCH);
    r_fullscreen = ri.Cvar_Get("r_fullscreen", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_noborder = ri.Cvar_Get("r_noborder", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_customwidth = ri.Cvar_Get("r_customwidth", "1600", CVAR_ARCHIVE | CVAR_LATCH);
    r_customheight = ri.Cvar_Get("r_customheight", "1024", CVAR_ARCHIVE | CVAR_LATCH);
    r_customPixelAspect = ri.Cvar_Get("r_customPixelAspect", "1", CVAR_ARCHIVE | CVAR_LATCH);
    r_ignorehwgamma = ri.Cvar_Get("r_ignorehwgamma", "0", CVAR_ARCHIVE | CVAR_LATCH);
    ri.Cvar_Get("r_availableModes", "", CVAR_ROM);

    if (ri.Cvar_VariableIntegerValue("com_abnormalExit")) {
        ri.Cvar_Set("r_mode", va("%d", VK_MODE_FALLBACK));
        ri.Cvar_Set("r_fullscreen", "0");
        ri.Cvar_Set("r_centerWindow", "0");
        ri.Cvar_Set("com_abnormalExit", "0");
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            ri.Error(ERR_FATAL, "VKimp_Init: SDL_Init( SDL_INIT_VIDEO ) failed (%s)", SDL_GetError());
        }
        ri.Printf(PRINT_ALL, "SDL using driver \"%s\"\n", SDL_GetCurrentVideoDriver());
        ri.Cvar_Set("r_sdlDriver", SDL_GetCurrentVideoDriver());
    }

    mode = r_mode->integer;

    if (VKimp_SetMode(config, mode, r_fullscreen->integer, r_noborder->integer) != VKSERR_OK) {
        // Same three-step retreat as the GL path: the requested mode, then the
        // same mode windowed and bordered, then 1024x768 windowed. A Vulkan
        // build that cannot open a window has nothing to print the failure on,
        // so this has to resolve to *something* or die saying why.
        ri.Printf(PRINT_ALL, "...WARNING: could not set mode %d, retrying windowed\n", mode);

        if (VKimp_SetMode(config, mode, qfalse, qfalse) != VKSERR_OK) {
            if (mode == VK_MODE_FALLBACK ||
                VKimp_SetMode(config, VK_MODE_FALLBACK, qfalse, qfalse) != VKSERR_OK) {
                // cl_renderer is CVAR_ARCHIVE, so the choice is already written
                // to the config. Leaving it there means every later launch dies
                // here too, with no way back except editing the config by hand -
                // the same trap the loader path had. Put it back before dying.
                ri.Cvar_Set("cl_renderer", "opengl2");

                ri.Error(ERR_FATAL,
                         "VKimp_Init() - could not create a Vulkan window. This usually means no "
                         "Vulkan driver is installed, or SDL has no Vulkan support for this video "
                         "driver. cl_renderer has been set back to opengl2; restart to use it.");
            }
        }
    }

    // These force the UI to disable driver selection, as the GL path does.
    config->driverType = GLDRV_ICD;
    config->hardwareType = GLHW_GENERIC;

    Q_strncpyz(config->vendor_string, "Vulkan", sizeof(config->vendor_string));
    Q_strncpyz(config->renderer_string, "Vulkan", sizeof(config->renderer_string));
    Q_strncpyz(config->version_string, "Vulkan", sizeof(config->version_string));
    config->extensions_string[0] = '\0';

    // vk.c overwrites these three with the real device strings once the
    // instance is up; until then something has to be there, because GfxInfo()
    // prints them before that happens.

    ri.IN_Init(SDL_window);
}

/*
===============
VKimp_InitGamma

ri.GLimp_InitGamma. The name is Quake3e's and is not GL-specific in any way -
it is SDL_SetWindowBrightness either way.
===============
*/
void VKimp_InitGamma(glconfig_t *config) {
    config->deviceSupportsGamma =
        (!r_ignorehwgamma || !r_ignorehwgamma->integer) && SDL_window != NULL && SDL_SetWindowBrightness(SDL_window, 1.0f) >= 0;
}

/*
===============
VKimp_Shutdown

ri.VKimp_Shutdown. unloadDLL is REF_UNLOAD_DLL from RE_Shutdown: on a
vid_restart the window is kept, on a real shutdown it goes.
===============
*/
void VKimp_Shutdown(qboolean unloadDLL) {
    ri.IN_Shutdown();

    if (unloadDLL) {
        if (SDL_window) {
            SDL_DestroyWindow(SDL_window);
            SDL_window = NULL;
        }
        SDL_Vulkan_UnloadLibrary();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

/*
===============
VKimp_CreateSurface

ri.VK_CreateSurface. The import table types these as void* / void** rather than
VkInstance / VkSurfaceKHR* so that tr_public.h, which every module in the tree
includes, does not need the Vulkan headers.
===============
*/
qboolean VKimp_CreateSurface(void *instance, void **surface) {
    if (!SDL_window) {
        return qfalse;
    }

    if (!SDL_Vulkan_CreateSurface(SDL_window, (VkInstance)instance, (VkSurfaceKHR *)surface)) {
        ri.Printf(PRINT_ALL, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return qfalse;
    }

    return qtrue;
}

/*
===============
VKimp_GetInstanceProcAddr

ri.VK_GetInstanceProcAddr. vk.c resolves every Vulkan entry point through this,
including the instance-less ones (vkCreateInstance, vkEnumerateInstance*), which
is why a null instance has to be passed straight through rather than rejected.
===============
*/
void *VKimp_GetInstanceProcAddr(void *instance, const char *name) {
    typedef void (*vkVoidFunction_t)(void);
    typedef vkVoidFunction_t (*vkGetInstanceProcAddr_t)(void *instance, const char *name);
    static vkGetInstanceProcAddr_t getProcAddr;

    if (!getProcAddr) {
        getProcAddr = (vkGetInstanceProcAddr_t)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (!getProcAddr) {
            ri.Error(ERR_FATAL, "SDL_Vulkan_GetVkGetInstanceProcAddr failed: %s", SDL_GetError());
        }
    }

    return (void *)getProcAddr(instance, name);
}
