#ifndef SDL_VKPROBE_H
#define SDL_VKPROBE_H

// [QL] E126. What the best Vulkan device on this machine can do - see
// sdl_vkprobe.c. All false when there is no Vulkan loader or no GPU.
typedef struct {
    qboolean usable;     // a GPU (not a CPU implementation) with Vulkan 1.1+
    qboolean discrete;   // that GPU is a discrete card
    qboolean rayQuery;   // VK_KHR_ray_query + VK_KHR_acceleration_structure
    char name[128];
} vkProbe_t;

qboolean Sys_ProbeVulkan(vkProbe_t* out);

#endif
