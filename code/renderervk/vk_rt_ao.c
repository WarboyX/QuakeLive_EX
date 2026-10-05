/*
===========================================================================
[QL] vk_rt_ao.c - ray-traced ambient occlusion (R13 onwards): its pipeline,
descriptors and denoise, the shared depth view and sampler (E153), and the
per-frame pass. Split out of vk.c unchanged.
===========================================================================
*/
#include "vk_local.h"



/*
=================
[QL] R13 step 3b: the ambient occlusion pass.

Descriptor set is {0: depth sampler, 1: the top-level acceleration structure}.
Push constants carry the inverse view-projection, the eye, and the four tuning
values, which is 96 bytes - inside the 128 every implementation guarantees, so
no uniform buffer and no per-frame descriptor churn.
=================
*/

typedef struct {
	float invViewProj[16];
	float eye[4];
	float params[4];      // radius, intensity, frame, bias
	float depthInfo[4];   // cleared depth, weapon band start, sign, unused
	float res[4];         // [QL] E150: x = trace scale (1 full, 2 half); 112 bytes in all
} rtaoPush_t;


/*
[QL] The frame's dynamic lights, so the trace can fade occlusion inside them.

Ambient occlusion estimates how much *ambient* light reaches a point. Where a
local light dominates the illumination, the ambient term is a small part of what
is there and its occlusion should not keep painting a contact shadow - which is
why occlusion reads as grime around a plasma bolt or a powerup rather than as
shading.

Each light is xyz = world origin, w = 1/(radius*radius), which is exactly the
form light_frag.tmpl uses for its own falloff. Same number, same shape, so the
region the occlusion fades in is the region the light actually lights, rather
than an approximation of it that has to be tuned to agree.

32 is well past what is on screen at once: the engine's own ceiling is 64 and a
plasma stream is the worst realistic case at around a dozen.
*/
#define RT_MAX_AO_LIGHTS 32

typedef struct {
	float params[4];                      // x = count, y = strength, zw unused
	float light[RT_MAX_AO_LIGHTS][4];     // xyz = origin, w = 1/(r*r)
} rtaoLights_t;

/* [QL] E196: the temporal pass's push constants - see rtao_temporal.tmpl. 96
   bytes: one matrix carries both views, so the 128-byte limit is not near. */
typedef struct {
	float reproject[16];   // this frame's clip -> last frame's clip
	float depthLinear[4];  // proj[10], proj[14], tolerance, aoScale
	float params[4];       // weight of this frame, history usable, full width, full height
} rtaoTemporalPush_t;

/* [QL] One line each per map, not per frame - 250 of these a second is not a
   diagnostic. Reset when the world is rebuilt, which is where a change of state
   would actually matter. */
qboolean rtaoOnReported = qfalse;
qboolean rtaoOffReported = qfalse;

void vk_rt_destroy_ao( void )
{
	if ( vk.rt.pipeline_gen != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_gen, NULL );
		vk.rt.pipeline_gen = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_blur != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_blur, NULL );
		vk.rt.pipeline_blur = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_gen_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_gen_half, NULL );
		vk.rt.pipeline_gen_half = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_blur_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_blur_half, NULL );
		vk.rt.pipeline_blur_half = VK_NULL_HANDLE;
	}
	/* [QL] E154 */
	if ( vk.rt.pipeline_ssao != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_ssao, NULL );
		vk.rt.pipeline_ssao = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_ssao_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_ssao_half, NULL );
		vk.rt.pipeline_ssao_half = VK_NULL_HANDLE;
	}
	if ( vk.rt.ssao_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.ssao_pipeline_layout, NULL );
		vk.rt.ssao_pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.ssao_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.ssao_set_layout, NULL );
		vk.rt.ssao_set_layout = VK_NULL_HANDLE;
	}
	vk.rt.ssao_descriptor = VK_NULL_HANDLE;   // freed with the pool below
	if ( vk.rt.pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline, NULL );
		vk.rt.pipeline = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_debug != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_debug, NULL );
		vk.rt.pipeline_debug = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.pipeline_layout, NULL );
		vk.rt.pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.blur_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.blur_pipeline_layout, NULL );
		vk.rt.blur_pipeline_layout = VK_NULL_HANDLE;
	}
	/* [QL] E196 */
	if ( vk.rt.pipeline_temporal != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_temporal, NULL );
		vk.rt.pipeline_temporal = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_temporal_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline_temporal_half, NULL );
		vk.rt.pipeline_temporal_half = VK_NULL_HANDLE;
	}
	if ( vk.rt.temporal_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.temporal_pipeline_layout, NULL );
		vk.rt.temporal_pipeline_layout = VK_NULL_HANDLE;
	}
	vk.rt.hist[0].valid = vk.rt.hist[1].valid = qfalse;
	{
		uint32_t li;
		for ( li = 0; li < ARRAY_LEN( vk.rt.light_buffer ); li++ ) {
			if ( vk.rt.light_buffer[li] != VK_NULL_HANDLE ) {
				qvkUnmapMemory( vk.device, vk.rt.light_memory[li] );
				qvkDestroyBuffer( vk.device, vk.rt.light_buffer[li], NULL );
				qvkFreeMemory( vk.device, vk.rt.light_memory[li], NULL );
				vk.rt.light_buffer[li] = VK_NULL_HANDLE;
				vk.rt.light_memory[li] = VK_NULL_HANDLE;
				vk.rt.light_ptr[li] = NULL;
			}
		}
	}

	/* the sets are freed with the pool, so they are not freed separately */
	if ( vk.rt.pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.rt.pool, NULL );
		vk.rt.pool = VK_NULL_HANDLE;
		vk.rt.descriptor[0] = VK_NULL_HANDLE;
		vk.rt.descriptor[1] = VK_NULL_HANDLE;
		Com_Memset( vk.rt.blur_descriptor, 0, sizeof( vk.rt.blur_descriptor ) );
		Com_Memset( vk.rt.temporal_descriptor, 0, sizeof( vk.rt.temporal_descriptor ) );   /* [QL] E196 */
	}
	if ( vk.rt.set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.set_layout, NULL );
		vk.rt.set_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.blur_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.blur_set_layout, NULL );
		vk.rt.blur_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.temporal_set_layout != VK_NULL_HANDLE ) {   /* [QL] E196 */
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.temporal_set_layout, NULL );
		vk.rt.temporal_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.ao_sampler != VK_NULL_HANDLE ) {
		qvkDestroySampler( vk.device, vk.rt.ao_sampler, NULL );
		vk.rt.ao_sampler = VK_NULL_HANDLE;
	}
	vk.rt.aoReady = qfalse;
}


/*
=================
[QL] E153. The depth view and sampler, shared by everything that reads depth.

They lived inside the ray-traced AO setup, so the water reflection - which
reads depth and fires no rays - borrowed them from a pass it has nothing to do
with. On a GPU without ray query they never existed and the water had no depth
to read; with it, an AO setup that disabled itself on any error destroyed the
view the water was still using. Their own create/destroy, run whenever depth
is sampleable, fixes both.
=================
*/
void vk_depth_sampling_destroy( void )
{
	if ( vk.rt.depth_sampler != VK_NULL_HANDLE ) {
		qvkDestroySampler( vk.device, vk.rt.depth_sampler, NULL );
		vk.rt.depth_sampler = VK_NULL_HANDLE;
	}
	if ( vk.rt.depth_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.rt.depth_view, NULL );
		vk.rt.depth_view = VK_NULL_HANDLE;
	}
}


void vk_depth_sampling_create( void )
{
	VkImageViewCreateInfo view_desc;
	VkSamplerCreateInfo sampler_desc;
	VkResult res;

	vk_depth_sampling_destroy();

	if ( !vk.rtDepthSampled || vk.depth_image == VK_NULL_HANDLE ) {
		return;
	}

	/* depth aspect only: a combined image sampler must name exactly one aspect,
	   and vk.depth_image_view carries DEPTH|STENCIL where there is stencil */
	Com_Memset( &view_desc, 0, sizeof( view_desc ) );
	view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_desc.image = vk.depth_image;
	view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_desc.format = vk.depth_format;
	view_desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	view_desc.subresourceRange.baseMipLevel = 0;
	view_desc.subresourceRange.levelCount = 1;
	view_desc.subresourceRange.baseArrayLayer = 0;
	view_desc.subresourceRange.layerCount = 1;

	res = qvkCreateImageView( vk.device, &view_desc, NULL, &vk.rt.depth_view );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "Depth sampling: view failed (%s) - AO and water reflections "
			"will not run\n", vk_result_string( res ) );
		vk.rt.depth_view = VK_NULL_HANDLE;
		return;
	}

	/* nearest and clamped: depth must never be filtered - see vk_rt_create_ao */
	Com_Memset( &sampler_desc, 0, sizeof( sampler_desc ) );
	sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_desc.magFilter = VK_FILTER_NEAREST;
	sampler_desc.minFilter = VK_FILTER_NEAREST;
	sampler_desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.maxAnisotropy = 1.0f;
	sampler_desc.minLod = 0.0f;
	sampler_desc.maxLod = 0.0f;
	sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

	res = qvkCreateSampler( vk.device, &sampler_desc, NULL, &vk.rt.depth_sampler );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "Depth sampling: sampler failed (%s)\n", vk_result_string( res ) );
		vk_depth_sampling_destroy();
	}
}


/*
=================
vk_rt_create_ao

Built after the attachments exist, because it needs a view onto the depth image.
Any failure leaves aoReady false and says why - the pass then does nothing and
the rest of the renderer is untouched.
=================
*/
void vk_rt_create_ao( void )
{
	VkDescriptorSetLayoutBinding bindings[3];
	VkDescriptorSetLayoutCreateInfo layout_desc;
	VkDescriptorPoolSize pool_sizes[3];
	VkDescriptorPoolCreateInfo pool_desc;
	VkDescriptorSetAllocateInfo set_alloc;
	VkPushConstantRange push_range;
	VkPipelineLayoutCreateInfo pl_desc;
	VkSamplerCreateInfo sampler_desc;
	VkResult res;
	uint32_t i;

	vk_rt_destroy_ao();

	/* [QL] E154: ray query is no longer required here - without it the pass
	   is built for screen-space AO, and every RT-only piece below is skipped */
	if ( !vk.rtDepthSampled || vk.depth_image == VK_NULL_HANDLE ) {
		return;
	}

	/*
	[QL] The render pass has to exist before a pipeline is built against it.

	Obvious, and it still cost a crash: vk_create_render_passes returns early
	when r_fbo is 0, the AO pass was created past that return, and the pipeline
	was then built with renderPass = VK_NULL_HANDLE without complaint. Nothing
	said a word until the first draw called vkCmdBeginRenderPass with a null
	handle. The cause is fixed - it is created on both paths now - but a missing
	render pass should disable the feature with a message rather than arm a
	pipeline that cannot be used, because the next early return in that function
	will not announce itself either.
	*/
	if ( vk.render_pass.rtao == VK_NULL_HANDLE || vk.render_pass.rtao_offscreen == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "RT AO: no %s render pass was created - disabling\n",
			vk.render_pass.rtao == VK_NULL_HANDLE ? "composite" : "offscreen" );
		return;
	}

	if ( vk.rt.ao_image_view[0] == VK_NULL_HANDLE || vk.rt.ao_image_view[1] == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "RT AO: the occlusion targets were not created - disabling\n" );
		return;
	}

	/* [QL] E153: the depth view and sampler are vk_depth_sampling_create's now */
	if ( vk.rt.depth_view == VK_NULL_HANDLE || vk.rt.depth_sampler == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "RT AO: no depth view to read - disabling\n" );
		vk_rt_destroy_ao();
		return;
	}

	/*
	NEAREST and CLAMP_TO_EDGE. Depth is not a colour and must not be filtered -
	averaging two depths produces a value describing a surface that is not
	there, half way between the two, and the AO would trace from inside
	geometry along every silhouette. The multisampled build does not use the
	sampler's filtering at all (texelFetch ignores it) but still needs one
	bound.
	*/
	Com_Memset( &sampler_desc, 0, sizeof( sampler_desc ) );
	sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_desc.magFilter = VK_FILTER_NEAREST;
	sampler_desc.minFilter = VK_FILTER_NEAREST;
	sampler_desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.maxAnisotropy = 1.0f;
	sampler_desc.minLod = 0.0f;
	sampler_desc.maxLod = 0.0f;
	sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;


	/*
	[QL] And one for the occlusion targets. Also NEAREST, and for a related
	reason: every tap the denoise takes is a texelFetch at an integer
	coordinate, so filtering would never be asked for - but a LINEAR sampler
	here would be a standing invitation for a later change to sample at
	half-texel offsets and silently get a filtered result the bilateral weights
	know nothing about. The sampler says what the shader is allowed to do.
	*/
	res = qvkCreateSampler( vk.device, &sampler_desc, NULL, &vk.rt.ao_sampler );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT AO: occlusion sampler failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	/* [QL] E154: set up outside the RT-only block below - the denoise and
	   screen-space layouts after it reuse this struct, and without ray query
	   the block does not run. */
	Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
	layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

	if ( vk.rtActive ) {   // [QL] E154: the trace's set - depth, the acceleration structure, the lights
		// ---- descriptor set layout ----
		Com_Memset( bindings, 0, sizeof( bindings ) );
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		/* [QL] the frame's dynamic lights - see rtaoLights_t */
		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[2].pImmutableSamplers = NULL;

		Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
		layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layout_desc.bindingCount = 3;
		layout_desc.pBindings = bindings;

		res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.rt.set_layout );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "RT AO: descriptor set layout failed (%s)\n", vk_result_string( res ) );
			vk_rt_destroy_ao();
			return;
		}
	}

	/*
	[QL] And the denoise layout: an occlusion target and depth, both sampled.

	Binding 1 is the same depth image the trace reads, because the blur has to
	know which neighbours are on the same surface and depth is the only thing
	this renderer has that says so - there is no normal buffer to consult.
	*/
	Com_Memset( bindings, 0, sizeof( bindings ) );
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	layout_desc.bindingCount = 2;
	layout_desc.pBindings = bindings;

	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.rt.blur_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT AO: denoise set layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	/* [QL] E154: screen-space AO's trace set - depth alone */
	layout_desc.bindingCount = 1;
	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.rt.ssao_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "AO: screen-space set layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	/* [QL] E196: the temporal pass - this frame's trace, depth, last frame's
	   history. Bindings 0 and 1 as the denoise has them. */
	bindings[2] = bindings[0];
	bindings[2].binding = 2;
	layout_desc.bindingCount = 3;
	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.rt.temporal_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "AO: temporal set layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	/*
	[QL] One light list per command buffer, host visible and written each frame.

	Created before the sets because the descriptor write names the buffer, and
	that write happens once rather than per frame - only the contents change.

	A failure here disables the whole pass rather than the feature, which is
	heavy-handed for a list of lights, but the alternative is a descriptor set
	with an unbound binding that the shader still reads.
	*/
	for ( i = 0; vk.rtActive && i < ARRAY_LEN( vk.rt.light_buffer ); i++ ) {   // [QL] E154: RT only
		if ( !rt_create_host_buffer( sizeof( rtaoLights_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				&vk.rt.light_buffer[i], &vk.rt.light_memory[i], &vk.rt.light_ptr[i] ) ) {
			ri.Printf( PRINT_WARNING, "RT AO: could not create the light list - disabling\n" );
			vk_rt_destroy_ao();
			return;
		}
		Com_Memset( vk.rt.light_ptr[i], 0, sizeof( rtaoLights_t ) );
	}

	// ---- pool and sets ----
	/* Three sets: the trace's, and one per occlusion target for the denoise. */
	Com_Memset( pool_sizes, 0, sizeof( pool_sizes ) );
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	/* trace: depth x2. denoise: 6 x (ao + depth). E154: SSAO depth. E196: temporal 4 x 3 */
	pool_sizes[0].descriptorCount = 2 + 12 + 1 + 12;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[1].descriptorCount = 2;   // one per command buffer
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[2].descriptorCount = 2;   // the light list, one per command buffer

	Com_Memset( &pool_desc, 0, sizeof( pool_desc ) );
	pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_desc.maxSets = 13;   // two trace sets, six denoise sets, [QL] E154 the screen-space set, E196 four temporal
	/* [QL] E154: without ray query there is no acceleration structure or light
	   list to pool for - only the first entry, the samplers */
	pool_desc.poolSizeCount = vk.rtActive ? 3 : 1;
	pool_desc.pPoolSizes = pool_sizes;

	res = qvkCreateDescriptorPool( vk.device, &pool_desc, NULL, &vk.rt.pool );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT AO: descriptor pool failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.rt.pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.rt.set_layout;

	for ( i = 0; vk.rtActive && i < ARRAY_LEN( vk.rt.descriptor ); i++ ) {   // [QL] E154: RT only
		res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.rt.descriptor[i] );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "RT AO: descriptor set %i failed (%s)\n", i, vk_result_string( res ) );
			vk_rt_destroy_ao();
			return;
		}
	}

	set_alloc.pSetLayouts = &vk.rt.blur_set_layout;
	for ( i = 0; i < ARRAY_LEN( vk.rt.blur_descriptor ); i++ ) {
		res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.rt.blur_descriptor[i] );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "RT AO: denoise set %i failed (%s)\n", i, vk_result_string( res ) );
			vk_rt_destroy_ao();
			return;
		}
	}

	/* [QL] E196 */
	set_alloc.pSetLayouts = &vk.rt.temporal_set_layout;
	for ( i = 0; i < ARRAY_LEN( vk.rt.temporal_descriptor ); i++ ) {
		res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.rt.temporal_descriptor[i] );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "AO: temporal set %i failed (%s)\n", i, vk_result_string( res ) );
			vk_rt_destroy_ao();
			return;
		}
	}

	/* [QL] E154: the screen-space set. Depth only, and depth does not change
	   under it, so it is written once here. */
	set_alloc.pSetLayouts = &vk.rt.ssao_set_layout;
	res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.rt.ssao_descriptor );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "AO: screen-space set failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}
	{
		VkDescriptorImageInfo depth_info;
		VkWriteDescriptorSet depth_write;

		Com_Memset( &depth_info, 0, sizeof( depth_info ) );
		depth_info.sampler = vk.rt.depth_sampler;
		depth_info.imageView = vk.rt.depth_view;
		depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		Com_Memset( &depth_write, 0, sizeof( depth_write ) );
		depth_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depth_write.dstSet = vk.rt.ssao_descriptor;
		depth_write.dstBinding = 0;
		depth_write.descriptorCount = 1;
		depth_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		depth_write.pImageInfo = &depth_info;
		qvkUpdateDescriptorSets( vk.device, 1, &depth_write, 0, NULL );
	}

	/*
	The denoise sets never change after this: they point at two images that live
	as long as the attachments do, and the acceleration structure - the one
	thing that changes per map - is not in them. Written here rather than in
	vk_rt_update_ao_descriptor for that reason.
	*/
	for ( i = 0; i < ARRAY_LEN( vk.rt.blur_descriptor ); i++ ) {
		VkDescriptorImageInfo blur_info[2];
		VkWriteDescriptorSet blur_writes[2];

		Com_Memset( blur_info, 0, sizeof( blur_info ) );
		blur_info[0].sampler = vk.rt.ao_sampler;
		blur_info[0].imageView = vk.rt.ao_image_view[i];
		blur_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		blur_info[1].sampler = vk.rt.depth_sampler;
		blur_info[1].imageView = vk.rt.depth_view;
		/* The layout depth is actually in during these draws: vk_rt_ao puts it
		   there with a barrier before the first of the three passes, and the
		   composite pass's attachment description agrees. */
		blur_info[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		Com_Memset( blur_writes, 0, sizeof( blur_writes ) );
		blur_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		blur_writes[0].dstSet = vk.rt.blur_descriptor[i];
		blur_writes[0].dstBinding = 0;
		blur_writes[0].descriptorCount = 1;
		blur_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		blur_writes[0].pImageInfo = &blur_info[0];

		blur_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		blur_writes[1].dstSet = vk.rt.blur_descriptor[i];
		blur_writes[1].dstBinding = 1;
		blur_writes[1].descriptorCount = 1;
		blur_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		blur_writes[1].pImageInfo = &blur_info[1];

		qvkUpdateDescriptorSets( vk.device, 2, blur_writes, 0, NULL );
	}

	/* [QL] E196: temporal set ch * 2 + p writes history 2 + ch * 2 + p and reads
	   2 + ch * 2 + (1 - p) - see vk.h */
	for ( i = 0; i < ARRAY_LEN( vk.rt.temporal_descriptor ); i++ ) {
		VkDescriptorImageInfo t_info[3];
		VkWriteDescriptorSet t_writes[3];
		uint32_t b;

		Com_Memset( t_info, 0, sizeof( t_info ) );
		t_info[0].sampler = vk.rt.ao_sampler;
		t_info[0].imageView = vk.rt.ao_image_view[0];
		t_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		t_info[1].sampler = vk.rt.depth_sampler;
		t_info[1].imageView = vk.rt.depth_view;
		t_info[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		t_info[2].sampler = vk.rt.ao_sampler;
		t_info[2].imageView = vk.rt.ao_image_view[ 2 + ( i / 2 ) * 2 + ( 1 - ( i & 1 ) ) ];
		t_info[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		Com_Memset( t_writes, 0, sizeof( t_writes ) );
		for ( b = 0; b < 3; b++ ) {
			t_writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			t_writes[b].dstSet = vk.rt.temporal_descriptor[i];
			t_writes[b].dstBinding = b;
			t_writes[b].descriptorCount = 1;
			t_writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			t_writes[b].pImageInfo = &t_info[b];
		}
		qvkUpdateDescriptorSets( vk.device, 3, t_writes, 0, NULL );
	}
	vk.rt.hist[0].valid = vk.rt.hist[1].valid = qfalse;   // new targets hold nothing yet

	// ---- pipeline layouts ----
	Com_Memset( &push_range, 0, sizeof( push_range ) );
	push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	push_range.offset = 0;
	push_range.size = sizeof( rtaoPush_t );

	Com_Memset( &pl_desc, 0, sizeof( pl_desc ) );
	pl_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_desc.setLayoutCount = 1;
	pl_desc.pSetLayouts = &vk.rt.set_layout;
	pl_desc.pushConstantRangeCount = 1;
	pl_desc.pPushConstantRanges = &push_range;

	/* [QL] E154: the screen-space trace takes the same push constants */
	pl_desc.pSetLayouts = &vk.rt.ssao_set_layout;
	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.rt.ssao_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "AO: screen-space pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}
	pl_desc.pSetLayouts = &vk.rt.set_layout;

	res = vk.rtActive ? qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.rt.pipeline_layout ) : VK_SUCCESS;
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT AO: pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	push_range.size = sizeof( rtaoBlurPush_t );
	pl_desc.pSetLayouts = &vk.rt.blur_set_layout;

	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.rt.blur_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT AO: denoise pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	/* [QL] E196 */
	push_range.size = sizeof( rtaoTemporalPush_t );
	pl_desc.pSetLayouts = &vk.rt.temporal_set_layout;
	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.rt.temporal_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "AO: temporal pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_rt_destroy_ao();
		return;
	}

	if ( vk.rtActive ) {   // [QL] E154: the ray-traced trace, full and half
		vk_create_post_process_pipeline( 4, glConfig.vidWidth, glConfig.vidHeight );
		vk_create_post_process_pipeline( 11, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );
	}
	/* [QL] E154: the screen-space trace, full and half */
	vk_create_post_process_pipeline( 16, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 17, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );
	vk_create_post_process_pipeline( 5, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 6, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 7, glConfig.vidWidth, glConfig.vidHeight );
	/* [QL] E150: half-resolution trace and horizontal denoise. Not fatal if
	   they fail - r_rtaoResolution 2 then just runs at full resolution. */
	vk_create_post_process_pipeline( 12, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );
	/* [QL] E196: optional - without them r_rtaoTemporal simply does nothing */
	vk_create_post_process_pipeline( 25, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 26, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );
	if ( ( vk.rt.pipeline_gen == VK_NULL_HANDLE && vk.rt.pipeline_ssao == VK_NULL_HANDLE ) ||
		vk.rt.pipeline_blur == VK_NULL_HANDLE ||
		vk.rt.pipeline == VK_NULL_HANDLE || vk.rt.pipeline_debug == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "RT AO: pipeline failed\n" );
		vk_rt_destroy_ao();
		return;
	}

	vk.rt.aoReady = qtrue;

	/* If a world is already loaded - which it is on a vid_restart mid-match -
	   point the set at its structure now. Harmless before the first map, where
	   the world build does it instead. */
	vk_rt_update_ao_descriptor();

	ri.Printf( PRINT_ALL, "AO: pass ready (%s depth; %s)\n",
		vkSamples != VK_SAMPLE_COUNT_1_BIT ? "multisampled" : "single-sample",
		vk.rt.pipeline_gen != VK_NULL_HANDLE ? "ray-traced and screen-space" : "screen-space only - no ray query" );
}


/*
=================
vk_rt_update_ao_descriptor

The depth view changes with every vid_restart and the acceleration structure
with every map, so the set is written when both exist rather than once.
=================
*/
void vk_rt_update_ao_descriptor( void )
{
	uint32_t n;
	VkWriteDescriptorSetAccelerationStructureKHR as_info;
	VkDescriptorImageInfo image_info;
	VkDescriptorBufferInfo light_info;
	VkWriteDescriptorSet writes[3];

	if ( !vk.rt.aoReady || vk.rt.world.tlas == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( &image_info, 0, sizeof( image_info ) );
	image_info.sampler = vk.rt.depth_sampler;
	image_info.imageView = vk.rt.depth_view;
	/* Must match the layout the image is actually in during the draw, which the
	   render pass puts at DEPTH_STENCIL_READ_ONLY_OPTIMAL for its subpass. */
	image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	/*
	[QL] One set per command buffer, each naming that frame's structure.

	With the dynamic structures built, each command buffer traces against its
	own top level - they are rebuilt every frame and sharing one would have a
	frame rebuilding the structure the frame before it is still reading.
	Without them, both name the static world structure and the two sets are
	identical, which costs nothing and keeps one code path.
	*/
	for ( n = 0; n < ARRAY_LEN( vk.rt.descriptor ); n++ ) {
		VkAccelerationStructureKHR as = vk.rt.world.dynReady
			? vk.rt.world.dyn_tlas[n] : vk.rt.world.tlas;

		if ( as == VK_NULL_HANDLE ) {
			continue;
		}

		Com_Memset( &as_info, 0, sizeof( as_info ) );
		as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		as_info.accelerationStructureCount = 1;
		as_info.pAccelerationStructures = &as;

		Com_Memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = vk.rt.descriptor[n];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &image_info;

		/* The acceleration structure rides in pNext rather than in a pBufferInfo
		   or pImageInfo - it is neither, and the handle is carried by the
		   extension struct. A write with descriptorType
		   ACCELERATION_STRUCTURE_KHR and no such pNext is silently nothing. */
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].pNext = &as_info;
		writes[1].dstSet = vk.rt.descriptor[n];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		/* [QL] this frame's dynamic lights - see rtaoLights_t. The buffer is
		   whole-size and rewritten per frame; only its contents change, so this
		   write happens once here rather than every frame. */
		Com_Memset( &light_info, 0, sizeof( light_info ) );
		light_info.buffer = vk.rt.light_buffer[n];
		light_info.offset = 0;
		light_info.range = sizeof( rtaoLights_t );

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = vk.rt.descriptor[n];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[2].pBufferInfo = &light_info;

		qvkUpdateDescriptorSets( vk.device, 3, writes, 0, NULL );
	}
}

/*
=================
[QL] rt_invert_matrix

General 4x4 inverse, column-major, matching the renderer's matrix convention.

Needed because the AO shader goes the other way round from everything else here:
every other matrix in this renderer takes world to clip, and reconstructing a
world position from a depth buffer takes clip back to world. Written as a
general inverse rather than by unpicking the projection's form, because the
projection here is reversed-depth and the form is exactly the sort of thing that
would be right on one machine and subtly wrong after the next change to it.

Verified against a reversed-depth projection times a rotated, translated view
matrix: VP * inv(VP) comes back as the identity to 1e-4.
=================
*/
qboolean rt_invert_matrix( const float *m, float *out )
{
	float inv[16], det;
	int i;

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if ( det == 0.0f ) {
		return qfalse;
	}
	det = 1.0f / det;
	for ( i = 0; i < 16; i++ ) {
		out[i] = inv[i] * det;
	}
	return qtrue;
}


/*
=================
vk_rt_ao

The ambient occlusion pass, run at the 3D-to-2D transition just before bloom.

Order matters and is the reason it sits here rather than at the end of post: AO
darkens the lit image, and bloom decides what is bright enough to glow. Bloom
first would let a corner bloom and *then* be darkened, which reads as light
leaking out of a shadow.

Leaves its own render pass open rather than reopening the main one. Reopening
main would clear - its load operations are baked in at creation and clear or
discard the colour - so the AO pass doubles as the pass everything after it
draws into, exactly as vk_bloom leaves post-bloom open for the 2D that follows.
=================
*/
/*
=================
[QL] E196. vk_rt_temporal

Blend this frame's trace (ao_image[0]) into channel ch's running average and
return the target that now holds it, for the denoise to read in place of the
trace - or 0, the trace itself, when the pass is off or cannot run. ch 0 is
the occlusion, 1 the sampled shadows. mode is the cvar: 1 on, 2 the
diagnostic view (see rtao_temporal.tmpl).

The history is only used when it was written on the frame just before this
one, at this trace scale: a menu, a map load, a toggle, a resolution change or
a pass that skipped a frame starts the average over. Must be called outside a
render pass, after the trace's has ended.
=================
*/
int vk_rt_temporal( int ch, int mode, int scale, const float *vp, const float *invViewProj, const float *proj )
{
	const VkPipeline tp = scale > 1 ? vk.rt.pipeline_temporal_half : vk.rt.pipeline_temporal;
	const int p = vk.rt.hist[ch].parity & 1;
	rtaoTemporalPush_t tpush;
	qboolean usable;

	if ( !mode || tp == VK_NULL_HANDLE || vk.rt.temporal_descriptor[ ch * 2 + p ] == VK_NULL_HANDLE ) {
		vk.rt.hist[ch].valid = qfalse;
		return 0;
	}
	usable = vk.rt.hist[ch].valid && vk.rt.hist[ch].scale == scale && vk.rt.hist[ch].frame == tr.frameCount - 1;

	myGlMultMatrix( invViewProj, vk.rt.hist[ch].viewproj, tpush.reproject );
	tpush.depthLinear[0] = proj[10];
	tpush.depthLinear[1] = proj[14];
	tpush.depthLinear[2] = 0.05f;
	tpush.depthLinear[3] = (float)scale;
	tpush.params[0] = mode == 2 ? -1.0f : 0.12f;   // this frame's share: about the last eight frames, weighted to the newest
	tpush.params[1] = usable ? 1.0f : 0.0f;
	tpush.params[2] = (float)glConfig.vidWidth;
	tpush.params[3] = (float)glConfig.vidHeight;

	vk_begin_rtao_offscreen_render_pass( 2 + ch * 2 + p, scale );
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tp );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.rt.temporal_pipeline_layout, 0, 1, &vk.rt.temporal_descriptor[ ch * 2 + p ], 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.temporal_pipeline_layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( tpush ), &tpush );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	Com_Memcpy( vk.rt.hist[ch].viewproj, vp, sizeof( vk.rt.hist[ch].viewproj ) );
	vk.rt.hist[ch].valid = qtrue;
	vk.rt.hist[ch].scale = scale;
	vk.rt.hist[ch].frame = tr.frameCount;
	vk.rt.hist[ch].parity = p ^ 1;
	return 2 + ch * 2 + p;
}


qboolean vk_rt_ao( void )
{
	rtaoPush_t push;
	rtaoBlurPush_t blur;
	float vp[16];
	float proj[16];
	int denoise;
	int src;   /* [QL] E196: the occlusion target the denoise reads */
	/* [QL] E154: which AO runs this frame. Ray-traced when it is ready and on;
	   otherwise screen-space when r_ssao asks for it - so a player with ray
	   query can still choose screen-space by turning ray-traced AO off. */
	const qboolean rtReady = vk.rt.aoReady && vk.rt.pipeline_gen != VK_NULL_HANDLE &&
		vk.rt.world.worldBuilt && vk.rt.world.tlas != VK_NULL_HANDLE;
	const qboolean useRT = rtReady && r_rtao && r_rtao->integer > 0;
	const qboolean useSSAO = !useRT && vk.rt.aoReady && vk.rt.pipeline_ssao != VK_NULL_HANDLE &&
		r_ssao && r_ssao->integer > 0;
	const qboolean debugView = useRT ? ( r_rtao->integer >= 2 ) : ( useSSAO && r_ssao->integer >= 2 );
	/* [QL] E150: r_rtaoResolution, and only if both half-size pipelines exist */
	int aoScale = ( r_rtaoResolution && r_rtaoResolution->integer >= 2 &&
		( useSSAO ? vk.rt.pipeline_ssao_half : vk.rt.pipeline_gen_half ) != VK_NULL_HANDLE &&
		vk.rt.pipeline_blur_half != VK_NULL_HANDLE ) ? 2 : 1;

	if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP ) {
		return qfalse;   // the little world-in-a-portal view, not the scene
	}
	if ( backEnd.doneRTAO || !backEnd.doneSurfaces ) {
		return qfalse;   // already run this frame, or there is no 3D yet
	}
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return qfalse;   // [QL] E175: a UI model view - no world to occlude, see vk_actor_shadows
	}

	/*
	[QL] Everything that can stop this, named individually, once per map.

	The first version of this printed one line - "everything is ready but
	r_rtao is 0" - and reached it only after three earlier conditions had
	already returned in silence. A field report of "I'm not seeing anything
	different" turned out to be one of those silent ones: vk.fboActive was
	false because r_fbo defaults to 0, so the pass returned on its first line
	every frame and said nothing at all, while every setup line in the log
	still read as success.

	One condition per message, because the fix differs for each and a single
	"not running" tells nobody which one to go and change.
	*/
	if ( !useRT && !useSSAO ) {
		if ( !rtaoOffReported ) {
			rtaoOffReported = qtrue;
			if ( !vk.rt.aoReady ) {
				ri.Printf( PRINT_ALL, "AO: not running - the pass was not created (see the Depth sampling lines above)\n" );
			} else if ( r_rtao && r_rtao->integer > 0 && !rtReady ) {
				ri.Printf( PRINT_ALL, "RT AO: not running - %s. r_ssao 1 gives screen-space AO instead.\n",
					vk.rt.pipeline_gen == VK_NULL_HANDLE ? "no ray query on this device"
					                                     : "no acceleration structure for this map" );
			} else {
				ri.Printf( PRINT_ALL, "AO: not running - r_rtao and r_ssao are both 0. Everything else is ready.\n" );
			}
		}
		return qfalse;
	}

	/* The other half of the same problem: when it does run, say so once, so a
	   log can tell "drew" from "was ready to draw". */
	{
		/* [QL] E150: r_rtaoResolution is live, so say it again when it moves */
		static int reportedScale = 0;
		if ( aoScale != reportedScale ) {
			reportedScale = aoScale;
			rtaoOnReported = qfalse;
		}
	}
	if ( !rtaoOnReported && useSSAO ) {
		rtaoOnReported = qtrue;
		ri.Printf( PRINT_ALL, "SSAO: running%s - 12 samples/pixel at %ix%i%s, radius %g, strength %g\n",
			debugView ? " (DEBUG VIEW: showing occlusion)" : "",
			( glConfig.vidWidth + aoScale - 1 ) / aoScale, ( glConfig.vidHeight + aoScale - 1 ) / aoScale,
			aoScale > 1 ? " (half resolution)" : "", r_rtaoRadius->value, r_rtaoIntensity->value );
	}
	if ( !rtaoOnReported ) {
		const int aoSamples = ri.Cvar_VariableIntegerValue( "r_rtaoSamples" );
		const int traceW = ( glConfig.vidWidth + aoScale - 1 ) / aoScale;
		const int traceH = ( glConfig.vidHeight + aoScale - 1 ) / aoScale;
		const double rays = (double)traceW * (double)traceH * (double)aoSamples;

		rtaoOnReported = qtrue;
		ri.Printf( PRINT_ALL, "RT AO: tracing%s - %i rays/pixel, radius %g, strength %g, denoise %s\n",
			r_rtao->integer >= 2 ? " (DEBUG VIEW: showing occlusion)" : "",
			aoSamples,
			r_rtaoRadius->value, r_rtaoIntensity->value,
			r_rtaoDenoise->integer == 0 ? "off" : ( r_rtaoDenoise->integer >= 2 ? "wide" : "on" ) );

		/*
		[QL] The number nobody was being shown.

		The trace is a full-resolution pass and the sample count is a
		specialization constant with the loop unrolled, so the cost is
		width x height x samples ray queries in one fragment shader, every
		frame. At 4K with the menu's top sample setting that is 133 million,
		which is more than most path tracers do per frame and well past what a
		single draw call can finish inside a driver watchdog.

		What that looks like from the outside is not slowness. The GPU is reset
		mid-draw, so the display stalls while sound and input keep running, and
		an application that resubmits the same work stalls again - reported as
		the whole machine seizing for minutes at a time, with nothing in the log
		to connect it to a slider in a menu.

		Every input to that number was already on the line above. None of them
		meant anything without being multiplied together, so multiply them.
		*/
		ri.Printf( PRINT_ALL, "RT AO: %.1fM rays/frame at %ix%i%s\n",
			rays / 1000000.0, traceW, traceH, aoScale > 1 ? " (half resolution)" : "" );

		if ( rays > 32000000.0 ) {
			ri.Printf( PRINT_WARNING, "RT AO: that is a very large trace for one draw call. If the "
				"display freezes for seconds at a time while sound keeps playing, the driver is "
				"resetting the GPU - lower " S_COLOR_CYAN "\\r_rtaoSamples" S_COLOR_YELLOW
				" (needs a vid_restart) or the resolution.\n" );
		}
		if ( !vk.fboActive ) {
			/* Not fatal - this pass only samples depth, unlike bloom, which
			   needs the colour attachment and is why r_fbo gates that one. Said
			   anyway, because it is the difference between MSAA being available
			   and not. */
			ri.Printf( PRINT_ALL, "RT AO: r_fbo is 0, so there is no multisampling to apply AO before\n" );
		}
	}

	/*
	World to clip, then inverted - and it has to be the *same* clip the depth
	buffer was rendered with, which is not viewParms.projectionMatrix as it
	stands.

	get_mvp_transform negates element 5 before use: Quake's projection is an
	OpenGL one and Vulkan's clip space has Y the other way up. Reconstructing
	with the unmodified matrix mirrors every position vertically, which does not
	look like a flip on screen - the ray origins slide smoothly across the view
	and the result is broad diagonal gradients over the walls, with the AO noise
	still visibly on top of them because the tracing itself is working fine.
	Copied rather than referenced, since this must not disturb what the renderer
	is about to draw with.

	The modelview is the world orientation, matching what tr_backend.c puts in
	vk_world.modelview_transform for world surfaces - the depth buffer holds the
	whole scene, not one model.
	*/
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	myGlMultMatrix( backEnd.viewParms.world.modelMatrix, proj, vp );
	if ( !rt_invert_matrix( vp, push.invViewProj ) ) {
		return qfalse;   // degenerate view, nothing sensible to reconstruct
	}

	VectorCopy( backEnd.viewParms.or.origin, push.eye );
	push.eye[3] = 0.0f;

	push.params[0] = r_rtaoRadius->value;
	push.params[1] = r_rtaoIntensity->value;
	/* A frame counter for the per-pixel rotation. Wrapped small deliberately:
	   it only has to differ between neighbouring frames, and a float that grows
	   without bound loses its low bits - which are the only part the hash
	   uses - after a few hours of uptime. */
	/* [QL] E196: tr.frameCount. This read vk.frame_count, which is not a frame
	   counter: it is vk_begin_frame's nesting guard, 1 for the whole of every
	   frame. The rotation was the same every frame since R13 - the grain never
	   moved, and nothing accumulating over frames could have averaged it.
	   It turns only while the temporal pass is on to average it: alone, a
	   pattern that moves every frame is shimmer, where a fixed one is grain. */
	push.params[2] = r_rtaoTemporal->integer ? (float)( tr.frameCount & 255 ) : 0.0f;
	push.params[3] = 1.5f;   // surface bias, in world units
	push.res[0] = (float)aoScale;   // [QL] E150
	/* [QL] E174: r_rtDynamic 2 - players and items occlude by their real
	   triangles (RT_MASK_SILHOUETTE) instead of the proxy boxes and balls,
	   which under an item drew a round blob that read as a second shadow */
	push.res[1] = (float)( ( r_rtDynamic->integer == 2 && vk.rt.world.actorReady && vk.rt.world.actorTris > 0 )
		? ( RT_MASK_LEVEL | RT_MASK_SILHOUETTE ) : RT_MASK_OCCLUSION );
	/* [QL] E202 traced one in K of the rays per frame while the temporal pass
	   averaged them; E208 took that back. On a still camera the average hid
	   it, but in play the history is rejected constantly and the one-ray
	   frames showed as noise flickering on a K-frame cycle (tester). The
	   shader still accepts a stride in res.z; 1 is the full set. */
	push.res[2] = 1.0f;
	push.res[3] = 0.0f;

	/*
	[QL] Which depth values are not surfaces, taken from the engine's own
	definitions rather than written out again in the shader.

	Both depend on USE_REVERSED_DEPTH, which flips the whole convention: with it
	the buffer is cleared to 0.0 and the near plane is 1.0, so the obvious test
	for "nothing here" - depth >= 1.0 - is exactly wrong and skips the near
	plane while tracing the sky. It was written that way, and the sky was traced
	from a position reconstructed at the far plane for several builds.

	The weapon band is the viewport depth range get_viewport applies for
	DEPTH_RANGE_WEAPON. Anything inside it is the view model, whose depth says
	where it was squashed to rather than where it is.
	*/
#ifdef USE_REVERSED_DEPTH
	push.depthInfo[0] = 0.0f;   // cleared to far
	push.depthInfo[1] = 0.6f;   // DEPTH_RANGE_WEAPON minDepth
	push.depthInfo[2] = 1.0f;   // near is 1.0
#else
	push.depthInfo[0] = 1.0f;
	push.depthInfo[1] = 0.3f;   // DEPTH_RANGE_WEAPON maxDepth
	push.depthInfo[2] = -1.0f;
#endif
	/* [QL] Which normal to trace around - see r_rtaoNormals. */
	/*
	[QL] Two toggles in one float, as a bitmask of small exact integers.

	Bit 0: use the derivative normal rather than the neighbour one.
	Bit 1: leave the view weapon out of the pass.

	Packed rather than given a field each because the push constant is already
	112 bytes against a 128 byte guarantee, and spending the last 16 on two
	booleans would leave nothing for the next thing that needs it. 0 to 3 are
	exactly representable, so comparing for equality in the shader is exact and
	not the usual float-compare hazard.
	*/
	push.depthInfo[3] = (float)( ( ( r_rtaoNormals->integer == 0 ) ? 1 : 0 ) |
	                             ( ( r_rtaoWeapon->integer == 0 ) ? 2 : 0 ) );

	/*
	[QL] What the denoise needs to turn a depth value into a distance:
	dist = proj[14] / (depth + proj[10]). Taken from the same matrix the scene
	was drawn with rather than from r_znear and r_zfar, which are the inputs to
	that matrix and not always the numbers that ended up in it - R_SetupProjection
	has four branches and the reversed-depth transform rewrites both terms
	afterwards.
	*/
	blur.depthLinear[0] = proj[10];
	blur.depthLinear[1] = proj[14];
	blur.depthLinear[2] = 0.05f;   // taps within 5% of the centre's distance
	blur.depthLinear[3] = (float)aoScale;   // [QL] E150

	denoise = ( r_rtaoDenoise->integer != 0 );

	/*
	[QL] This frame's dynamic lights, for the trace to fade occlusion inside.

	Read here rather than anywhere later because the occlusion pass now runs
	before the lit surface pass, and RB_LightingPass clears
	backEnd.viewParms.num_dlights on its way out. backEnd.refdef keeps its own
	count and is not cleared, so this reads that one and is correct either way.

	dl->origin is world space, which is the space the trace reconstructs
	positions in. dl->transformed is the same light in eye space and is what
	the lit pass wants; picking the wrong one of those would put every
	suppression sphere somewhere near the camera instead of near its light, so
	it is worth saying which is which.
	*/
	if ( vk.rt.light_ptr[ vk.cmd_index ] != NULL ) {
		rtaoLights_t *lights = (rtaoLights_t *)vk.rt.light_ptr[ vk.cmd_index ];
		float strength = r_rtaoLights->value;
		int count = 0;

		if ( strength > 0.0f ) {
			int li;

			if ( strength > 1.0f ) {
				strength = 1.0f;
			}

			for ( li = 0; li < backEnd.refdef.num_dlights && count < RT_MAX_AO_LIGHTS; li++ ) {
				const dlight_t *dl = &backEnd.refdef.dlights[li];

				if ( dl->radius <= 0.0f ) {
					continue;
				}
				lights->light[count][0] = dl->origin[0];
				lights->light[count][1] = dl->origin[1];
				lights->light[count][2] = dl->origin[2];
				/* 1/(r*r) - the same falloff term light_frag.tmpl uses, so the
				   region occlusion fades in is the region the light lights. */
				lights->light[count][3] = 1.0f / ( dl->radius * dl->radius );
				count++;
			}
		}

		lights->params[0] = (float)count;
		lights->params[1] = strength;
		/* [QL] E165: lights clear occlusion only where they arrive, when they
		   cast shadows - see rtao.tmpl */
		lights->params[2] = ( r_rtDlightShadows->integer || R_SHADOWS_TRACED ) ? 1.0f : 0.0f;
		lights->params[3] = (float)( RT_SHADOW_MASK );
	}

	vk_timing_begin( RTT_AO );   /* [QL] E177 */
	vk_end_render_pass();   // end main

	/*
	[QL] R13 step 4: the entities, into this frame's top level structure.

	Here because an acceleration structure build cannot be inside a render pass,
	and the gap between the main pass ending and the occlusion pass beginning is
	the only point in the frame that is outside one and still before the trace.

	Built every frame whether or not entities are wanted in it. Skipping the
	build when r_rtDynamic is 0 would leave the structure holding whatever the
	last frame that did build left in it, and the trace reading that - so the
	cvar instead controls only whether the entity instances are added. Off, the
	structure holds the map and nothing else, which is exactly what the static
	one held, for the price of rebuilding a one-instance structure per frame.
	*/
	vk_rt_build_dynamic_tlas();

	/*
	[QL] Depth becomes a texture, once, here.

	The first two passes sample it outside any render pass, so it has to be
	moved out of DEPTH_STENCIL_ATTACHMENT_OPTIMAL explicitly - there is no
	subpass to do it implicitly. The barrier is also the synchronisation: it is
	what makes the main pass's depth writes visible to the fragment shader, in
	place of the subpass dependency that did that job when the AO pass shared
	the main framebuffer.

	The composite pass's attachment description declares depth as arriving in
	this layout and leaves it in ATTACHMENT_OPTIMAL, so nothing downstream sees
	a difference.
	*/
	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		glConfig.stencilBits ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		0, 0 );

	// ---- pass 1: trace, into ao_image[0] ----
	vk_timing_begin( RTT_AO_TRACE );   /* [QL] E206 */
	vk_begin_rtao_offscreen_render_pass( 0, aoScale );

	if ( useSSAO ) {   /* [QL] E154: the depth-only trace */
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			aoScale > 1 ? vk.rt.pipeline_ssao_half : vk.rt.pipeline_ssao );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.rt.ssao_pipeline_layout, 0, 1, &vk.rt.ssao_descriptor, 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.ssao_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	} else {
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			aoScale > 1 ? vk.rt.pipeline_gen_half : vk.rt.pipeline_gen );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.rt.pipeline_layout, 0, 1, &vk.rt.descriptor[ vk.cmd_index ], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	}
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_render_pass();
	vk_timing_end( RTT_AO_TRACE );

	/*
	[QL] E196: temporal accumulation, ao_image[0] + last frame's history ->
	this frame's history (2 + parity), which the denoise then reads instead of
	the raw trace. See rtao_temporal.tmpl. The history is only used when it was
	written on the frame just before this one, at this resolution: a menu, a
	map load, a toggle or a resolution change starts the average over.
	*/
	src = vk_rt_temporal( 0, r_rtaoTemporal->integer, aoScale, vp, push.invViewProj, proj );

	// ---- pass 2: horizontal denoise, ao_image[src] -> ao_image[1] ----
	/*
	Skipped entirely when the denoise is off, rather than run with a zero step.
	The composite below then reads target 0 instead of target 1, which is the
	only thing that changes - there is no second path and no second pipeline.
	*/
	if ( denoise ) {
		blur.step[0] = 1.0f;
		blur.step[1] = 0.0f;
		blur.step[2] = 1.0f;   // intermediate pass: no output scaling
		blur.step[3] = 0.0f;   // [QL] E150: writes at the occlusion's own resolution

		vk_begin_rtao_offscreen_render_pass( 1, aoScale );

		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			aoScale > 1 ? vk.rt.pipeline_blur_half : vk.rt.pipeline_blur );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.rt.blur_pipeline_layout, 0, 1, &vk.rt.blur_descriptor[ src ], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.blur_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( blur ), &blur );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

		vk_end_render_pass();
	}

	// ---- pass 3: vertical denoise and composite, into the scene ----
	blur.step[0] = 0.0f;
	blur.step[1] = denoise ? 1.0f : 0.0f;   // a zero step is the shader's passthrough
	/*
	[QL] The debug view has to survive the present pass to be worth looking at.

	It replaces the scene with the occlusion term, and then the present pass
	does what it does to every pixel: multiply by obScale and apply the response
	curve. With r_rts that is a multiply by two into a curve that compresses
	everything above 0.8, so an occlusion term of 0.5 and one of 0.9 both come
	out near white and the view shows only the deepest creases. It reads as a
	much tighter radius, and it was reported as one - the same settings looked
	like a different radius at r_rts 0 and r_rts 1.

	Pre-dividing by obScale cancels the multiply, so what reaches the screen is
	the value the trace produced. The composite keeps 1.0, because there this
	value is the blend source and scaling it would scale the occlusion itself.
	*/
	blur.step[2] = ( debugView && tr.overbrightBits > 0 )
		? 1.0f / (float)( 1 << tr.overbrightBits )
		: 1.0f;
	blur.step[3] = 1.0f;   // [QL] E150: writes at full resolution (the upsample)

	vk_timing_begin( RTT_AO_COMP );   /* [QL] E206 */
	vk_begin_rtao_render_pass();

	/* r_rtao 2 shows the occlusion term itself instead of its effect. Grey with
	   dark creases means it is working; flat white means every ray is missing;
	   an unchanged scene means the pass never drew. */
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		debugView ? vk.rt.pipeline_debug : vk.rt.pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.rt.blur_pipeline_layout, 0, 1, &vk.rt.blur_descriptor[ denoise ? 1 : src ], 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.blur_pipeline_layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( blur ), &blur );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_composite_render_pass();   // [QL] E150: depth writable for the rest of the frame
	vk_timing_end( RTT_AO_COMP );
	vk_timing_end( RTT_AO );   /* [QL] E177 */

	/*
	Put back what the pass clobbered. This is what ate the HUD.

	Binding a descriptor set with vk.rt.pipeline_layout invalidates whatever the
	geometry path had bound at those indices, and pushing constants with it
	invalidates the MVP - both are "incompatible layout" in the spec's sense.
	The renderer only re-binds a descriptor when its *value* changes, tracked as
	a dirty range in vk.cmd->descriptor_set, so after this pass the values still
	matched, the range was empty, and nothing was re-bound. Everything drawn
	afterwards - which is the entire 2D pass - used a binding that no longer
	existed, and the HUD simply did not appear.

	The first attempt at this copied vk_bloom's restore loop, gated on
	last_pipeline != VK_NULL_HANDLE. That gate can never be true here:
	vk_begin_render_pass sets last_pipeline to VK_NULL_HANDLE itself, and one
	was begun four lines ago. The loop was dead code from the moment it was
	written, which is exactly the shape that does not announce itself.

	Marking the whole range dirty is better than restoring by hand anyway - it
	uses the machinery that already exists, and it cannot get the arguments
	wrong the way an open-coded vkCmdBindDescriptorSets can.
	*/
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;

	/* The MVP push constant belongs to a different layout and is gone too.
	   last_pipeline is already VK_NULL_HANDLE, so the next draw rebinds the
	   pipeline; this puts the transform back with it. */
	vk_update_mvp( NULL );

	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	backEnd.doneRTAO = qtrue;

	return qtrue;
}
