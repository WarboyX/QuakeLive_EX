/*
===========================================================================
[QL] vk_local.h - shared between the Vulkan backend's own files only.

vk.c used to be one 16,600-line file, and everything in it was static. It is
split by subsystem now (vk_timing.c, vk_rt_world.c, vk_rt_ao.c,
vk_rt_shadow.c, vk_ssr.c), and this header is what those files share: the
Vulkan entry points vk.c loads, and the handful of helpers more than one of
them calls. It is not part of the renderer's interface - tr_*.c include vk.h,
never this.
===========================================================================
*/
#ifndef VK_LOCAL_H
#define VK_LOCAL_H

#include "tr_local.h"
#include "vk.h"

#if defined (_DEBUG)
#if defined (_WIN32)
#define USE_VK_VALIDATION
#include <windows.h> // for win32 debug callback
#endif
#endif

#define VK_CHECK( function_call ) { \
	VkResult res = function_call; \
	if ( res < 0 ) { \
		ri.Error( ERR_FATAL, "Vulkan: %s returned %s", #function_call, vk_result_string( res ) ); \
	} \
}

// debug markers
#define SET_OBJECT_NAME(obj,objName,objType) vk_set_object_name( (uint64_t)(obj), (objName), (objType) )

/*
[QL] R13 step 3c: what the denoise passes are pushed (AO and shadow passes).

Deliberately not sharing rtaoPush_t. The blur needs none of the trace's 96 bytes
of matrix and eye position, and the two shaders declare different blocks -
handing one the other's layout is the same class of mistake as feeding the AO
shader the gamma shader's specialization constants, which cost a round already.
*/
typedef struct {
	float step[4];         // xy = texel step along the axis being blurred
	float depthLinear[4];  // proj[10], proj[14], depth tolerance, [QL] E150 occlusion scale
} rtaoBlurPush_t;

// the main pass's MSAA sample count, chosen in vk.c
extern int vkSamples;

// Vulkan API functions, loaded by vk.c
extern PFN_vkCreateInstance								qvkCreateInstance;
extern PFN_vkEnumerateInstanceExtensionProperties		qvkEnumerateInstanceExtensionProperties;
extern PFN_vkEnumerateInstanceLayerProperties			qvkEnumerateInstanceLayerProperties;
extern PFN_vkCreateDevice								qvkCreateDevice;
extern PFN_vkDestroyInstance							qvkDestroyInstance;
extern PFN_vkEnumerateDeviceExtensionProperties			qvkEnumerateDeviceExtensionProperties;
extern PFN_vkEnumeratePhysicalDevices					qvkEnumeratePhysicalDevices;
extern PFN_vkGetDeviceProcAddr							qvkGetDeviceProcAddr;
extern PFN_vkGetPhysicalDeviceFeatures					qvkGetPhysicalDeviceFeatures;
extern PFN_vkGetPhysicalDeviceFormatProperties			qvkGetPhysicalDeviceFormatProperties;
extern PFN_vkGetPhysicalDeviceMemoryProperties			qvkGetPhysicalDeviceMemoryProperties;
extern PFN_vkGetPhysicalDeviceProperties				qvkGetPhysicalDeviceProperties;
extern PFN_vkGetPhysicalDeviceQueueFamilyProperties		qvkGetPhysicalDeviceQueueFamilyProperties;
extern PFN_vkDestroySurfaceKHR							qvkDestroySurfaceKHR;
extern PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceFormatsKHR			qvkGetPhysicalDeviceSurfaceFormatsKHR;
extern PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	qvkGetPhysicalDeviceSurfacePresentModesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceSupportKHR			qvkGetPhysicalDeviceSurfaceSupportKHR;
#ifdef USE_VK_VALIDATION
extern PFN_vkCreateDebugReportCallbackEXT				qvkCreateDebugReportCallbackEXT;
extern PFN_vkDestroyDebugReportCallbackEXT				qvkDestroyDebugReportCallbackEXT;
#endif
extern PFN_vkAllocateCommandBuffers						qvkAllocateCommandBuffers;
extern PFN_vkAllocateDescriptorSets						qvkAllocateDescriptorSets;
extern PFN_vkAllocateMemory								qvkAllocateMemory;
extern PFN_vkBeginCommandBuffer							qvkBeginCommandBuffer;
extern PFN_vkBindBufferMemory							qvkBindBufferMemory;
extern PFN_vkBindImageMemory							qvkBindImageMemory;
extern PFN_vkCmdBeginRenderPass							qvkCmdBeginRenderPass;
extern PFN_vkCmdBindDescriptorSets						qvkCmdBindDescriptorSets;
extern PFN_vkCmdBindIndexBuffer							qvkCmdBindIndexBuffer;
extern PFN_vkCmdBindPipeline							qvkCmdBindPipeline;
extern PFN_vkCmdBindVertexBuffers						qvkCmdBindVertexBuffers;
extern PFN_vkCmdBlitImage								qvkCmdBlitImage;
extern PFN_vkCmdClearAttachments						qvkCmdClearAttachments;
extern PFN_vkCmdCopyBuffer								qvkCmdCopyBuffer;
extern PFN_vkCmdCopyBufferToImage						qvkCmdCopyBufferToImage;
extern PFN_vkCmdCopyImage								qvkCmdCopyImage;
extern PFN_vkCmdDraw									qvkCmdDraw;
extern PFN_vkCmdDrawIndexed								qvkCmdDrawIndexed;
extern PFN_vkCmdEndRenderPass							qvkCmdEndRenderPass;
extern PFN_vkCmdNextSubpass								qvkCmdNextSubpass;
extern PFN_vkCmdPipelineBarrier							qvkCmdPipelineBarrier;
extern PFN_vkCreateQueryPool							qvkCreateQueryPool;
extern PFN_vkDestroyQueryPool							qvkDestroyQueryPool;
extern PFN_vkCmdResetQueryPool							qvkCmdResetQueryPool;
extern PFN_vkCmdWriteTimestamp							qvkCmdWriteTimestamp;
extern PFN_vkGetQueryPoolResults						qvkGetQueryPoolResults;
extern PFN_vkCmdPushConstants							qvkCmdPushConstants;
extern PFN_vkCmdSetDepthBias							qvkCmdSetDepthBias;
extern PFN_vkCmdSetScissor								qvkCmdSetScissor;
extern PFN_vkCmdSetViewport								qvkCmdSetViewport;
extern PFN_vkCreateBuffer								qvkCreateBuffer;
extern PFN_vkCreateCommandPool							qvkCreateCommandPool;
extern PFN_vkCreateDescriptorPool						qvkCreateDescriptorPool;
extern PFN_vkCreateDescriptorSetLayout					qvkCreateDescriptorSetLayout;
extern PFN_vkCreateFence								qvkCreateFence;
extern PFN_vkCreateFramebuffer							qvkCreateFramebuffer;
extern PFN_vkCreateGraphicsPipelines					qvkCreateGraphicsPipelines;
extern PFN_vkCreateImage								qvkCreateImage;
extern PFN_vkCreateImageView							qvkCreateImageView;
extern PFN_vkCreatePipelineLayout						qvkCreatePipelineLayout;
extern PFN_vkCreatePipelineCache						qvkCreatePipelineCache;
extern PFN_vkCreateRenderPass							qvkCreateRenderPass;
extern PFN_vkCreateSampler								qvkCreateSampler;
extern PFN_vkCreateSemaphore							qvkCreateSemaphore;
extern PFN_vkCreateShaderModule							qvkCreateShaderModule;
extern PFN_vkDestroyBuffer								qvkDestroyBuffer;
extern PFN_vkDestroyCommandPool							qvkDestroyCommandPool;
extern PFN_vkDestroyDescriptorPool						qvkDestroyDescriptorPool;
extern PFN_vkDestroyDescriptorSetLayout					qvkDestroyDescriptorSetLayout;
extern PFN_vkDestroyDevice								qvkDestroyDevice;
extern PFN_vkDestroyFence								qvkDestroyFence;
extern PFN_vkDestroyFramebuffer							qvkDestroyFramebuffer;
extern PFN_vkDestroyImage								qvkDestroyImage;
extern PFN_vkDestroyImageView							qvkDestroyImageView;
extern PFN_vkDestroyPipeline							qvkDestroyPipeline;
extern PFN_vkDestroyPipelineCache						qvkDestroyPipelineCache;
extern PFN_vkGetPipelineCacheData						qvkGetPipelineCacheData;
extern PFN_vkDestroyPipelineLayout						qvkDestroyPipelineLayout;
extern PFN_vkDestroyRenderPass							qvkDestroyRenderPass;
extern PFN_vkDestroySampler								qvkDestroySampler;
extern PFN_vkDestroySemaphore							qvkDestroySemaphore;
extern PFN_vkDestroyShaderModule						qvkDestroyShaderModule;
extern PFN_vkDeviceWaitIdle								qvkDeviceWaitIdle;
extern PFN_vkEndCommandBuffer							qvkEndCommandBuffer;
extern PFN_vkFlushMappedMemoryRanges					qvkFlushMappedMemoryRanges;
extern PFN_vkFreeCommandBuffers							qvkFreeCommandBuffers;
extern PFN_vkFreeDescriptorSets							qvkFreeDescriptorSets;
extern PFN_vkFreeMemory									qvkFreeMemory;
extern PFN_vkGetBufferMemoryRequirements				qvkGetBufferMemoryRequirements;
extern PFN_vkGetDeviceQueue								qvkGetDeviceQueue;
extern PFN_vkGetImageMemoryRequirements					qvkGetImageMemoryRequirements;
extern PFN_vkGetImageSubresourceLayout					qvkGetImageSubresourceLayout;
extern PFN_vkInvalidateMappedMemoryRanges				qvkInvalidateMappedMemoryRanges;
extern PFN_vkMapMemory									qvkMapMemory;
extern PFN_vkQueueSubmit								qvkQueueSubmit;
extern PFN_vkQueueWaitIdle								qvkQueueWaitIdle;
extern PFN_vkResetCommandBuffer							qvkResetCommandBuffer;
extern PFN_vkResetDescriptorPool						qvkResetDescriptorPool;
extern PFN_vkResetFences								qvkResetFences;
extern PFN_vkUnmapMemory								qvkUnmapMemory;
extern PFN_vkUpdateDescriptorSets						qvkUpdateDescriptorSets;
extern PFN_vkWaitForFences								qvkWaitForFences;
extern PFN_vkAcquireNextImageKHR						qvkAcquireNextImageKHR;
extern PFN_vkCreateSwapchainKHR							qvkCreateSwapchainKHR;
extern PFN_vkDestroySwapchainKHR						qvkDestroySwapchainKHR;
extern PFN_vkGetSwapchainImagesKHR						qvkGetSwapchainImagesKHR;
extern PFN_vkQueuePresentKHR							qvkQueuePresentKHR;
extern PFN_vkGetBufferMemoryRequirements2KHR			qvkGetBufferMemoryRequirements2KHR;
extern PFN_vkGetImageMemoryRequirements2KHR				qvkGetImageMemoryRequirements2KHR;
extern PFN_vkDebugMarkerSetObjectNameEXT				qvkDebugMarkerSetObjectNameEXT;
extern PFN_vkCreateAccelerationStructureKHR				qvkCreateAccelerationStructureKHR;
extern PFN_vkDestroyAccelerationStructureKHR			qvkDestroyAccelerationStructureKHR;
extern PFN_vkGetAccelerationStructureBuildSizesKHR		qvkGetAccelerationStructureBuildSizesKHR;
extern PFN_vkCmdBuildAccelerationStructuresKHR			qvkCmdBuildAccelerationStructuresKHR;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR	qvkGetAccelerationStructureDeviceAddressKHR;
extern PFN_vkGetBufferDeviceAddressKHR					qvkGetBufferDeviceAddressKHR;

// vk_timing.c - r_rtTimings (E177)
void vk_timing_create( const VkPhysicalDeviceProperties *props );
void vk_timing_destroy( void );
void vk_timing_frame_start( void );

// vk.c helpers the split files share
const char *vk_result_string( VkResult code );
void vk_set_object_name( uint64_t obj, const char *objName, VkDebugReportObjectTypeEXT objType );
void record_image_layout_transition( VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags image_aspect_flags, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t src_stage_override, uint32_t dst_stage_override );
qboolean rt_create_host_buffer( VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory, void **mapped );
qboolean rt_invert_matrix( const float *m, float *out );
qboolean vk_rt_build_dynamic_tlas( void );
void vk_begin_rtao_render_pass( void );
void vk_end_composite_render_pass( void );
qboolean vk_format_has_alpha( VkFormat format );
// vk_ssr.c - water planes and the reflection pass (R19). Called from vk.c's
// render-pass setup, vk_initialize and the teardown paths.
void vk_ssr_create_render_pass( VkDevice device );
void vk_ssr_create( void );
void vk_ssr_destroy( void );

// render passes shared with the RT files
void vk_begin_render_pass( VkRenderPass renderPass, VkFramebuffer frameBuffer, qboolean clearValues, uint32_t width, uint32_t height );
void vk_begin_rtao_offscreen_render_pass( int target, int scale );
// descriptor updates each pass owns, called whenever the structure changes
void vk_ssr_update_rt_descriptor( void );          // vk_ssr.c, E156
void vk_actor_shadow_update_descriptor( void );    // vk_rt_shadow.c, E166

// vk_rt_ao.c - once-per-map AO reports, reset by the world rebuild
extern qboolean rtaoOnReported;
extern qboolean rtaoOffReported;

// vk_rt_ao.c - AO setup and teardown, the shared depth view
void vk_rt_destroy_ao( void );
void vk_depth_sampling_destroy( void );
void vk_depth_sampling_create( void );
void vk_rt_create_ao( void );
void vk_rt_update_ao_descriptor( void );

#endif // VK_LOCAL_H
