/*
===========================================================================
[QL] E126. Ask Vulkan what the GPU is, without loading the Vulkan renderer.

OpenGL 2 stays the default renderer so a low-end machine starts on something
it can run. A machine that can do better is offered Vulkan once, after launch
(see CL_CheckHardwarePrompt). Deciding that needs to know the GPU before the
Vulkan renderer has ever been loaded, and the OpenGL renderer cannot say: its
vendor and renderer strings name a chip but not whether it is discrete, what
Vulkan version it has, or whether it can do ray queries.

So this goes to the Vulkan loader directly - through SDL, which already knows
where the loader lives on every platform - creates a bare instance (no
surface, no device), reads each physical device's properties and extension
list, and tears it all down again. It costs a few milliseconds, once, and
only while the prompt is still unanswered.

No link-time dependency: every Vulkan entry point is fetched through
vkGetInstanceProcAddr, the way renderervk does it.
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#include "SDL.h"
#include "SDL_vulkan.h"
#else
#include <SDL.h>
#include <SDL_vulkan.h>
#endif

#define VK_NO_PROTOTYPES
#include "../renderercommon/vulkan/vulkan.h"

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sdl_vkprobe.h"

qboolean Sys_ProbeVulkan(vkProbe_t* out) {
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance createInstance;
    PFN_vkDestroyInstance destroyInstance;
    PFN_vkEnumeratePhysicalDevices enumDevices;
    PFN_vkGetPhysicalDeviceProperties getProps;
    PFN_vkEnumerateDeviceExtensionProperties enumExts;
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice devices[16];
    uint32_t count = ARRAY_LEN(devices);
    uint32_t i;
    int bestScore = -1;

    Com_Memset(out, 0, sizeof(*out));

    if (SDL_Vulkan_LoadLibrary(NULL) != 0) {
        Com_DPrintf("vkprobe: no Vulkan loader (%s)\n", SDL_GetError());
        return qfalse;
    }

    gipa = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    createInstance = gipa ? (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance") : NULL;
    if (!createInstance) {
        SDL_Vulkan_UnloadLibrary();
        return qfalse;
    }

    Com_Memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "quakelive hardware probe";
    app.apiVersion = VK_API_VERSION_1_1;

    Com_Memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    if (createInstance(&ci, NULL, &instance) != VK_SUCCESS) {
        Com_DPrintf("vkprobe: vkCreateInstance failed\n");
        SDL_Vulkan_UnloadLibrary();
        return qfalse;
    }

    destroyInstance = (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
    enumDevices = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    getProps = (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    enumExts = (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");

    if (enumDevices && getProps && enumExts &&
        enumDevices(instance, &count, devices) >= VK_SUCCESS) {   // VK_INCOMPLETE is fine
        for (i = 0; i < count; i++) {
            VkPhysicalDeviceProperties p;
            static VkExtensionProperties exts[512];   // ~130 KB: not on the stack
            uint32_t n = ARRAY_LEN(exts), j;
            qboolean rq = qfalse, as = qfalse;
            int score;

            getProps(devices[i], &p);
            if (enumExts(devices[i], NULL, &n, exts) >= VK_SUCCESS) {
                for (j = 0; j < n; j++) {
                    if (!strcmp(exts[j].extensionName, "VK_KHR_ray_query")) rq = qtrue;
                    if (!strcmp(exts[j].extensionName, "VK_KHR_acceleration_structure")) as = qtrue;
                }
            }

            // Pick the device the renderer would want: discrete first, then
            // integrated; a CPU implementation (llvmpipe, SwiftShader) never.
            switch (p.deviceType) {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 3; break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 2; break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 1; break;
                default: score = -1; break;
            }
            if (score < 0 || VK_API_VERSION_MINOR(p.apiVersion) + 10 * VK_API_VERSION_MAJOR(p.apiVersion) < 11) {
                continue;
            }
            if (rq && as) {
                score += 4;
            }
            if (score > bestScore) {
                bestScore = score;
                out->usable = qtrue;
                out->discrete = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
                out->rayQuery = (rq && as);
                Q_strncpyz(out->name, p.deviceName, sizeof(out->name));
            }
        }
    }

    if (destroyInstance) {
        destroyInstance(instance, NULL);
    }
    SDL_Vulkan_UnloadLibrary();

    return out->usable;
}
