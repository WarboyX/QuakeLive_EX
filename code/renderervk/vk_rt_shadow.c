/*
===========================================================================
[QL] vk_rt_shadow.c - the traced shadow pass: players and items on the
level, the level on itself, the sun as a second light, and the penumbra
setup (E166 onwards; the shader is actorshadow.tmpl). Split out of vk.c
unchanged.
===========================================================================
*/
#include "vk_local.h"



/*
=================
vk_actor_shadow_*

[QL] E166: players' and items' traced shadows on the level - see
actorshadow.tmpl for what the pass computes and vk.actorShadow for its pieces.
=================
*/
typedef struct {
	float invViewProj[16];
	float eye[4];
	float depthInfo[4];
	float params[4];
	float gridOrigin[4];
	float gridInvSize[4];
	float gridBounds[4];
	float soft[4];        // [QL] E167: x light size (world units), y rays
	float actorInfo[4];   // [QL] E204: x sphere count (-1 none listed), y reach
	float actors[RT_MAX_SHADOW_ACTORS][4];
} actorShadowUniform_t;

void vk_actor_shadow_destroy( void )
{
	uint32_t i;

	vk.actorShadow.ready = qfalse;
	if ( vk.actorShadow.pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.actorShadow.pipeline, NULL );
		vk.actorShadow.pipeline = VK_NULL_HANDLE;
	}
	if ( vk.actorShadow.pipeline_offscreen != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.actorShadow.pipeline_offscreen, NULL );
		vk.actorShadow.pipeline_offscreen = VK_NULL_HANDLE;
	}
	/* [QL] E171 */
	{
		VkPipeline *pens[] = { &vk.actorShadow.pen_trace, &vk.actorShadow.pen_filter, &vk.actorShadow.pen_composite };
		for ( i = 0; i < ARRAY_LEN( pens ); i++ ) {
			if ( *pens[i] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, *pens[i], NULL );
				*pens[i] = VK_NULL_HANDLE;
			}
		}
	}
	if ( vk.actorShadow.pen_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.actorShadow.pen_pipeline_layout, NULL );
		vk.actorShadow.pen_pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.actorShadow.pen_pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.actorShadow.pen_pool, NULL );
		vk.actorShadow.pen_pool = VK_NULL_HANDLE;
		Com_Memset( vk.actorShadow.pen_descriptor, 0, sizeof( vk.actorShadow.pen_descriptor ) );
	}
	if ( vk.actorShadow.pen_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.actorShadow.pen_set_layout, NULL );
		vk.actorShadow.pen_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.actorShadow.pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.actorShadow.pipeline_layout, NULL );
		vk.actorShadow.pipeline_layout = VK_NULL_HANDLE;
	}
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.actorShadow.uniform_buffer[i] != VK_NULL_HANDLE ) {
			qvkUnmapMemory( vk.device, vk.actorShadow.uniform_memory[i] );
			qvkDestroyBuffer( vk.device, vk.actorShadow.uniform_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.actorShadow.uniform_memory[i], NULL );
			vk.actorShadow.uniform_buffer[i] = VK_NULL_HANDLE;
			vk.actorShadow.uniform_memory[i] = VK_NULL_HANDLE;
			vk.actorShadow.uniform_ptr[i] = NULL;
		}
	}
	if ( vk.actorShadow.pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.actorShadow.pool, NULL );
		vk.actorShadow.pool = VK_NULL_HANDLE;
		Com_Memset( vk.actorShadow.descriptor, 0, sizeof( vk.actorShadow.descriptor ) );
	}
	if ( vk.actorShadow.set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.actorShadow.set_layout, NULL );
		vk.actorShadow.set_layout = VK_NULL_HANDLE;
	}
}

/* the per-map half: this map's per-frame structures and light grid */
void vk_actor_shadow_update_descriptor( void )
{
	uint32_t n;

	vk.actorShadow.ready = qfalse;
	if ( vk.actorShadow.pool == VK_NULL_HANDLE || !vk.rt.world.dynReady ||
		vk.rt.world.grid_buffer == VK_NULL_HANDLE ) {
		return;
	}
	for ( n = 0; n < NUM_COMMAND_BUFFERS; n++ ) {
		VkWriteDescriptorSetAccelerationStructureKHR as_info;
		VkDescriptorBufferInfo grid_info;
		VkWriteDescriptorSet writes[2];

		if ( vk.rt.world.dyn_tlas[n] == VK_NULL_HANDLE ) {
			return;
		}
		Com_Memset( &as_info, 0, sizeof( as_info ) );
		as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		as_info.accelerationStructureCount = 1;
		as_info.pAccelerationStructures = &vk.rt.world.dyn_tlas[n];

		Com_Memset( &grid_info, 0, sizeof( grid_info ) );
		grid_info.buffer = vk.rt.world.grid_buffer;
		grid_info.range = VK_WHOLE_SIZE;

		Com_Memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].pNext = &as_info;
		writes[0].dstSet = vk.actorShadow.descriptor[n];
		writes[0].dstBinding = 1;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = vk.actorShadow.descriptor[n];
		writes[1].dstBinding = 3;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = &grid_info;
		qvkUpdateDescriptorSets( vk.device, 2, writes, 0, NULL );
	}
	vk.actorShadow.ready = ( vk.actorShadow.pipeline != VK_NULL_HANDLE ) ? qtrue : qfalse;
}

/*
[QL] E171: the projected soft edge's set layout, sets, layout and pipelines.

The sets never change after this: [i] reads target i and depth, both of which
live as long as the attachments, and this is rebuilt with them. Everything is
optional - without it r_rtShadowSoftMode 1 falls back to the sampled edge.
*/
static void vk_pen_create( void )
{
	VkDescriptorSetLayoutBinding bindings[2];
	VkDescriptorSetLayoutCreateInfo layout_desc;
	VkDescriptorPoolSize pool_size;
	VkDescriptorPoolCreateInfo pool_desc;
	VkDescriptorSetAllocateInfo set_alloc;
	VkPipelineLayoutCreateInfo pl_desc;
	VkPushConstantRange push_range;
	uint32_t i;

	if ( vk.actorShadow.pen_pass == VK_NULL_HANDLE || vk.actorShadow.pen_fb[1] == VK_NULL_HANDLE ||
		vk.modules.penumbra_fs == VK_NULL_HANDLE || vk.modules.actor_shadow_pen_fs == VK_NULL_HANDLE ||
		vk.rt.ao_sampler == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( bindings, 0, sizeof( bindings ) );
	for ( i = 0; i < 2; i++ ) {
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	}
	Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
	layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_desc.bindingCount = 2;
	layout_desc.pBindings = bindings;
	if ( qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.actorShadow.pen_set_layout ) < 0 ) {
		goto fail;
	}

	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_size.descriptorCount = 2 * ARRAY_LEN( vk.actorShadow.pen_descriptor );
	Com_Memset( &pool_desc, 0, sizeof( pool_desc ) );
	pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_desc.maxSets = ARRAY_LEN( vk.actorShadow.pen_descriptor );
	pool_desc.poolSizeCount = 1;
	pool_desc.pPoolSizes = &pool_size;
	if ( qvkCreateDescriptorPool( vk.device, &pool_desc, NULL, &vk.actorShadow.pen_pool ) < 0 ) {
		goto fail;
	}

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.actorShadow.pen_pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.actorShadow.pen_set_layout;
	for ( i = 0; i < ARRAY_LEN( vk.actorShadow.pen_descriptor ); i++ ) {
		VkDescriptorImageInfo info[2];
		VkWriteDescriptorSet writes[2];
		uint32_t k;

		if ( qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.actorShadow.pen_descriptor[i] ) < 0 ) {
			goto fail;
		}
		Com_Memset( info, 0, sizeof( info ) );
		info[0].sampler = vk.rt.ao_sampler;
		info[0].imageView = vk.actorShadow.pen_view[i];
		info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info[1].sampler = vk.rt.depth_sampler;
		info[1].imageView = vk.rt.depth_view;
		info[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		Com_Memset( writes, 0, sizeof( writes ) );
		for ( k = 0; k < 2; k++ ) {
			writes[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[k].dstSet = vk.actorShadow.pen_descriptor[i];
			writes[k].dstBinding = k;
			writes[k].descriptorCount = 1;
			writes[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[k].pImageInfo = &info[k];
		}
		qvkUpdateDescriptorSets( vk.device, 2, writes, 0, NULL );
	}

	Com_Memset( &push_range, 0, sizeof( push_range ) );
	push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	push_range.size = sizeof( float ) * 8;
	Com_Memset( &pl_desc, 0, sizeof( pl_desc ) );
	pl_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_desc.setLayoutCount = 1;
	pl_desc.pSetLayouts = &vk.actorShadow.pen_set_layout;
	pl_desc.pushConstantRangeCount = 1;
	pl_desc.pPushConstantRanges = &push_range;
	if ( qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.actorShadow.pen_pipeline_layout ) < 0 ) {
		goto fail;
	}

	vk_create_post_process_pipeline( 22, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 23, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 24, glConfig.vidWidth, glConfig.vidHeight );
	if ( vk.actorShadow.pen_trace == VK_NULL_HANDLE || vk.actorShadow.pen_filter == VK_NULL_HANDLE ||
		vk.actorShadow.pen_composite == VK_NULL_HANDLE ) {
		goto fail;
	}
	return;

fail:
	ri.Printf( PRINT_WARNING, "RT: projected soft shadows not created - soft edges stay sampled\n" );
	/* the pipelines, layout, pool and set layout; vk_actor_shadow_destroy frees the same */
	{
		VkPipeline *pens[] = { &vk.actorShadow.pen_trace, &vk.actorShadow.pen_filter, &vk.actorShadow.pen_composite };
		for ( i = 0; i < ARRAY_LEN( pens ); i++ ) {
			if ( *pens[i] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, *pens[i], NULL );
				*pens[i] = VK_NULL_HANDLE;
			}
		}
	}
	if ( vk.actorShadow.pen_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.actorShadow.pen_pipeline_layout, NULL );
		vk.actorShadow.pen_pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.actorShadow.pen_pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.actorShadow.pen_pool, NULL );
		vk.actorShadow.pen_pool = VK_NULL_HANDLE;
	}
	if ( vk.actorShadow.pen_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.actorShadow.pen_set_layout, NULL );
		vk.actorShadow.pen_set_layout = VK_NULL_HANDLE;
	}
}

/* the per-device half: set layout, sets (depth and parameters written), pipeline */
void vk_actor_shadow_create( void )
{
	VkDescriptorSetLayoutBinding bindings[4];
	VkDescriptorSetLayoutCreateInfo layout_desc;
	VkDescriptorPoolSize pool_sizes[4];
	VkDescriptorPoolCreateInfo pool_desc;
	VkDescriptorSetAllocateInfo set_alloc;
	VkPipelineLayoutCreateInfo pl_desc;
	uint32_t i;

	vk_actor_shadow_destroy();

	if ( !vk.rtActive || vk.render_pass.rtao == VK_NULL_HANDLE || vk.rt.depth_view == VK_NULL_HANDLE ||
		vk.modules.actor_shadow_fs == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( bindings, 0, sizeof( bindings ) );
	for ( i = 0; i < 4; i++ ) {
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
	layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_desc.bindingCount = 4;
	layout_desc.pBindings = bindings;
	if ( qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.actorShadow.set_layout ) < 0 ) {
		goto fail;
	}

	for ( i = 0; i < 4; i++ ) {
		pool_sizes[i].type = bindings[i].descriptorType;
		pool_sizes[i].descriptorCount = NUM_COMMAND_BUFFERS;
	}
	Com_Memset( &pool_desc, 0, sizeof( pool_desc ) );
	pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_desc.maxSets = NUM_COMMAND_BUFFERS;
	pool_desc.poolSizeCount = 4;
	pool_desc.pPoolSizes = pool_sizes;
	if ( qvkCreateDescriptorPool( vk.device, &pool_desc, NULL, &vk.actorShadow.pool ) < 0 ) {
		goto fail;
	}

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.actorShadow.pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.actorShadow.set_layout;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		VkDescriptorImageInfo image_info;
		VkDescriptorBufferInfo buffer_info;
		VkWriteDescriptorSet writes[2];

		if ( qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.actorShadow.descriptor[i] ) < 0 ) {
			goto fail;
		}
		if ( !rt_create_host_buffer( sizeof( actorShadowUniform_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				&vk.actorShadow.uniform_buffer[i], &vk.actorShadow.uniform_memory[i], &vk.actorShadow.uniform_ptr[i] ) ) {
			goto fail;
		}

		/* depth as the occlusion composite reads it: still the pass's
		   (read-only) depth attachment while it is sampled */
		Com_Memset( &image_info, 0, sizeof( image_info ) );
		image_info.sampler = vk.rt.depth_sampler;
		image_info.imageView = vk.rt.depth_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		Com_Memset( &buffer_info, 0, sizeof( buffer_info ) );
		buffer_info.buffer = vk.actorShadow.uniform_buffer[i];
		buffer_info.range = sizeof( actorShadowUniform_t );

		Com_Memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = vk.actorShadow.descriptor[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &image_info;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = vk.actorShadow.descriptor[i];
		writes[1].dstBinding = 2;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[1].pBufferInfo = &buffer_info;
		qvkUpdateDescriptorSets( vk.device, 2, writes, 0, NULL );
	}

	Com_Memset( &pl_desc, 0, sizeof( pl_desc ) );
	pl_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_desc.setLayoutCount = 1;
	pl_desc.pSetLayouts = &vk.actorShadow.set_layout;
	if ( qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.actorShadow.pipeline_layout ) < 0 ) {
		goto fail;
	}

	vk_create_post_process_pipeline( 20, glConfig.vidWidth, glConfig.vidHeight );
	if ( vk.actorShadow.pipeline == VK_NULL_HANDLE ) {
		goto fail;
	}
	/* [QL] E169: only where AO's targets and denoise exist to hand it to;
	   without them soft shadows are drawn straight in, as before */
	if ( vk.rt.aoReady && vk.render_pass.rtao_offscreen != VK_NULL_HANDLE ) {
		vk_create_post_process_pipeline( 21, glConfig.vidWidth, glConfig.vidHeight );
	}
	vk_pen_create();   /* [QL] E171: not fatal - the sampled soft edge stays */

	vk_actor_shadow_update_descriptor();   /* a map already loaded (vid_restart) */
	return;

fail:
	ri.Printf( PRINT_WARNING, "RT: player/item shadow pass not created - players will cast no traced shadows\n" );
	vk_actor_shadow_destroy();
}

/* per frame, after the opaque surfaces, main view only */
qboolean vk_actor_shadows( void )
{
	actorShadowUniform_t *u;
	float proj[16], vp[16];
	qboolean denoise, projected;
	int src = 0;   /* [QL] E196 */

	/* [QL] E167: one pass for both - players and items (silhouettes) and the
	   level itself, traced toward the same estimated light */
	const qboolean actors = ( r_rtActorShadows->integer || R_SHADOWS_TRACED ) &&
		vk.rt.world.actorReady && vk.rt.world.actorTris > 0;
	const qboolean level = ( r_rtLevelShadows->integer || R_SHADOWS_TRACED ) ? qtrue : qfalse;

	if ( !actors && !level ) {
		return qfalse;
	}
	if ( !vk.actorShadow.ready || !backEnd.doneRTDynamic || vk.renderPassIndex == RENDER_PASS_SCREENMAP ) {
		return qfalse;
	}
	/*
	[QL] E175: the world view only, once a frame.

	A view with no world in it - the player model on the in-game menu's Player
	page, drawn by the UI as a scene of its own after the world - came through
	here too. This is a fullscreen pass: it ran with that view's camera over
	the whole screen's world depth, reconstructing nonsense positions (NaN
	among them) and multiplying them into the scene. In the float scene target
	NaN goes through bloom and the tone curve as coloured speckle over the whole
	frame, for as long as that page is open - the tester's "corrupted frame
	buffer, only on the Player tab". AO and the water already ran once a frame;
	this did not.
	*/
	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) || backEnd.doneActorShadows ) {
		return qfalse;
	}
	backEnd.doneActorShadows = qtrue;
	vk_timing_begin( RTT_SHADOW );   /* [QL] E177 */
	u = (actorShadowUniform_t *)vk.actorShadow.uniform_ptr[ vk.cmd_index ];
	if ( u == NULL ) {
		return qfalse;
	}

	/* the clip the depth buffer was drawn with - see vk_ssr for the proj[5] flip */
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	myGlMultMatrix( backEnd.viewParms.world.modelMatrix, proj, vp );
	if ( !rt_invert_matrix( vp, u->invViewProj ) ) {
		return qfalse;
	}
	VectorCopy( backEnd.viewParms.or.origin, u->eye );
	u->eye[3] = 0.0f;
#ifdef USE_REVERSED_DEPTH
	u->depthInfo[0] = 0.0f; u->depthInfo[1] = 0.6f; u->depthInfo[2] = 1.0f;
#else
	u->depthInfo[0] = 1.0f; u->depthInfo[1] = 0.3f; u->depthInfo[2] = -1.0f;
#endif
	u->depthInfo[3] = 0.0f;
	u->params[0] = r_rtActorShadowStrength->value;
	u->params[1] = r_rtActorShadowLength->value;
	u->params[2] = (float)( ( actors ? RT_MASK_SILHOUETTE : 0 ) | ( level ? RT_MASK_LEVEL : 0 ) );
	u->params[3] = ( r_rtActorShadows->integer >= 2 || r_rtLevelShadows->integer >= 2 ) ? 1.0f : 0.0f;
	/* [QL] E169 */
	denoise = ( u->soft[0] = r_rtActorShadowSoftness->value ) > 0.0f && u->params[3] == 0.0f &&
		r_rtActorShadowDenoise->integer && vk.actorShadow.pipeline_offscreen != VK_NULL_HANDLE &&
		vk.rt.aoReady && vk.rt.pipeline_blur != VK_NULL_HANDLE && vk.rt.pipeline != VK_NULL_HANDLE;
	/* [QL] E171: or projected, which needs neither AO nor the ray count */
	projected = u->soft[0] > 0.0f && u->params[3] == 0.0f && r_rtActorShadowSoftMode->integer == 1 &&
		vk.actorShadow.pen_composite != VK_NULL_HANDLE;
	u->soft[1] = (float)r_rtActorShadowRays->integer;
	/* [QL] E202 halved these rays while r_rtShadowTemporal averaged them;
	   E207 took that back. A shadow edge moves with every step its caster
	   takes, so history is rejected far more often than for occlusion, and two
	   rays a frame showed through the blur as streaks and blotches in the
	   tester's screenshots. The E204 early-out made the pass cheap where there
	   is nothing to shadow, which is where the saving mattered. */
	u->soft[2] = r_rtShadowSun->integer ? 1.0f : 0.0f;   /* [QL] E178 */
	/* [QL] E204: where the silhouettes are, so a pixel nowhere near one need
	   not trace - see actorshadow.tmpl. Only meaningful when the mesh was
	   built this frame; otherwise say "not listed" and trace as before. */
	if ( actors && vk.rt.world.actorReady && vk.rt.world.actorSphereCount >= 0 && r_rtCull->integer ) {
		u->actorInfo[0] = (float)vk.rt.world.actorSphereCount;
		Com_Memcpy( u->actors, vk.rt.world.actorSphere, sizeof( float ) * 4 * vk.rt.world.actorSphereCount );
	} else {
		u->actorInfo[0] = -1.0f;
	}
	{
		/* the furthest a ray from this pixel can meet a player: the light is at
		   most the reach away (lamp or sun), plus the disc a soft light's rays
		   spread over (softness, times reach/128 for the sun - discScale in
		   actorshadow.tmpl), plus the surface offsets; and lamps trace players
		   to 1.25x the light's distance + 32 (E186). Anything further cannot
		   shadow this pixel by construction. */
		const float reach = r_rtActorShadowLength->value;
		const float disc = r_rtActorShadowSoftness->value * ( reach / 128.0f > 1.0f ? reach / 128.0f : 1.0f );
		u->actorInfo[1] = 1.25f * ( reach + disc + 8.0f ) + 32.0f;
	}
	u->actorInfo[2] = u->actorInfo[3] = 0.0f;
	/* [QL] E196: the ray pattern turns each frame while r_rtShadowTemporal is
	   averaging it (sampled soft edge only); fixed otherwise, as before - a
	   pattern that moves by itself is shimmer, where a fixed one is grain */
	u->soft[3] = r_rtShadowTemporal->integer ? (float)( tr.frameCount & 255 ) : 0.0f;
	if ( vk.rt.world.haveGrid && tr.world ) {
		VectorCopy( tr.world->lightGridOrigin, u->gridOrigin );
		VectorCopy( tr.world->lightGridInverseSize, u->gridInvSize );
		u->gridBounds[0] = (float)tr.world->lightGridBounds[0];
		u->gridBounds[1] = (float)tr.world->lightGridBounds[1];
		u->gridBounds[2] = (float)tr.world->lightGridBounds[2];
		u->gridOrigin[3] = 1.0f;
	} else {
		u->gridOrigin[3] = 0.0f;
	}
	u->gridInvSize[3] = 0.0f;
	/* [QL] E176/E187: 1 - the light field and its light positions; 2 - the field
	   is there but r_rtLightField is off, so only what is not a position is
	   read (the sun and where it reaches, for r_rtShadowSun); 0 - no field. It
	   was 0 whenever r_rtLightField was off, and the sun's shadow went with it
	   with nothing to say so. */
	u->gridBounds[3] = !vk.rt.world.haveLightField ? 0.0f : ( r_rtLightField->integer ? 1.0f : 2.0f );

	vk_end_render_pass();
	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		glConfig.stencilBits ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		0, 0 );

	/*
	[QL] E169: soft edges through AO's denoise.

	A soft shadow is a handful of rays at different points on the light, and
	four rays is five shades of grey: without smoothing the penumbra is a
	speckle, which is how "softness does not work" looked on screen. AO has the
	same problem and already solves it - a two-pass blur that stops at depth
	edges, so a shadow does not bleed off a step onto the wall behind it. The
	AO pass is finished with its two targets by now, so the shadow borrows
	them: traced into target 0, blurred across into 1, blurred down and
	multiplied into the scene by AO's own composite.

	Not for hard shadows (nothing to smooth, and the blur would soften them),
	nor for the debug view (the targets hold one channel and the view is red).
	*/
	if ( projected ) {
		/* [QL] E171: centre ray -> search -> across -> down and in. See
		   penumbra.tmpl for the geometry. */
		float push[8];

		push[1] = r_rtActorShadowSoftness->value;
		push[2] = 64.0f;                                         // widest edge, pixels
		push[3] = proj[0] * (float)glConfig.vidWidth * 0.5f;     // pixels per unit at distance 1
		push[4] = proj[10];
		push[5] = proj[14];
		push[6] = 0.05f;
		push[7] = 0.0f;

		vk.renderWidth = glConfig.vidWidth;
		vk.renderHeight = glConfig.vidHeight;
		vk.renderScaleX = vk.renderScaleY = 1.0f;

		vk_begin_render_pass( vk.actorShadow.pen_pass, vk.actorShadow.pen_fb[0], qfalse, vk.renderWidth, vk.renderHeight );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pen_trace );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pipeline_layout, 0, 1, &vk.actorShadow.descriptor[ vk.cmd_index ], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		push[0] = 0.0f;   // search: target 0 -> 1
		vk_begin_render_pass( vk.actorShadow.pen_pass, vk.actorShadow.pen_fb[1], qfalse, vk.renderWidth, vk.renderHeight );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pen_filter );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pen_pipeline_layout, 0, 1, &vk.actorShadow.pen_descriptor[0], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.actorShadow.pen_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), push );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		push[0] = 1.0f;   // across: target 1 -> 0
		vk_begin_render_pass( vk.actorShadow.pen_pass, vk.actorShadow.pen_fb[0], qfalse, vk.renderWidth, vk.renderHeight );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pen_filter );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pen_pipeline_layout, 0, 1, &vk.actorShadow.pen_descriptor[1], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.actorShadow.pen_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), push );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		push[0] = 2.0f;   // down, into the scene: target 0
		vk_timing_begin( RTT_SH_COMP );   /* [QL] E206 */
		vk_begin_rtao_render_pass();
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pen_composite );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pen_pipeline_layout, 0, 1, &vk.actorShadow.pen_descriptor[0], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.actorShadow.pen_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), push );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	} else if ( denoise ) {
		rtaoBlurPush_t blur;

		blur.depthLinear[0] = proj[10];
		blur.depthLinear[1] = proj[14];
		blur.depthLinear[2] = 0.05f;
		blur.depthLinear[3] = 1.0f;

		vk_timing_begin( RTT_SH_TRACE );   /* [QL] E206 */
		vk_begin_rtao_offscreen_render_pass( 0, 1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pipeline_offscreen );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pipeline_layout, 0, 1, &vk.actorShadow.descriptor[ vk.cmd_index ], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();
		vk_timing_end( RTT_SH_TRACE );

		/* [QL] E196: averaged over frames, then denoised as before */
		src = vk_rt_temporal( 1, r_rtShadowTemporal->integer, 1, vp, u->invViewProj, proj );

		blur.step[0] = 1.0f; blur.step[1] = 0.0f; blur.step[2] = 1.0f; blur.step[3] = 0.0f;
		vk_begin_rtao_offscreen_render_pass( 1, 1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rt.pipeline_blur );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.rt.blur_pipeline_layout, 0, 1, &vk.rt.blur_descriptor[ src ], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.blur_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( blur ), &blur );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		blur.step[0] = 0.0f; blur.step[1] = 1.0f; blur.step[2] = 1.0f; blur.step[3] = 1.0f;
		vk_timing_begin( RTT_SH_COMP );   /* [QL] E206 */
		vk_begin_rtao_render_pass();
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rt.pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.rt.blur_pipeline_layout, 0, 1, &vk.rt.blur_descriptor[1], 0, NULL );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.rt.blur_pipeline_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( blur ), &blur );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	} else {
		vk_timing_begin( RTT_SH_COMP );   /* [QL] E206 */
		vk_begin_rtao_render_pass();
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.actorShadow.pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.actorShadow.pipeline_layout, 0, 1, &vk.actorShadow.descriptor[ vk.cmd_index ], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	}

	vk_end_composite_render_pass();
	vk_timing_end( RTT_SH_COMP );

	vk_timing_end( RTT_SHADOW );   /* [QL] E177 */

	/* what the AO pass says about the geometry path's bindings after a
	   foreign layout applies here word for word - see the end of vk_rt_ao */
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	vk_update_mvp( NULL );
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	return qtrue;
}
