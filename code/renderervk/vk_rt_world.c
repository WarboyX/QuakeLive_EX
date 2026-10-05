/*
===========================================================================
[QL] vk_rt_world.c - the ray-tracing acceleration structures (R13 onwards):
the static world, round proxies, players and items as their real triangles
(E166) with their holes capped (E180/E184), the per-frame dynamic structure,
and the light grid and light field upload that ride with the world build.
Split out of vk.c unchanged.
===========================================================================
*/
#include "vk_local.h"



/*
===========================================================================
[QL] R13 step 2: acceleration structures for the static world.

A bottom-level structure holding every opaque world triangle, and a top-level
structure with one instance of it at identity. Built once when the map loads,
freed when it unloads. Nothing traces against it yet - that is the AO pass -
but the geometry is the bulk of the work and is what everything else stands on.

Positions and indices only. A ray query for ambient occlusion asks "is anything
in the way", so normals, texcoords and lightmap coordinates would be bytes no
trace ever reads.
===========================================================================
*/

/* Only needed while ray query is on, and fetched rather than added to the
   instance function table: this is core 1.1, the instance is known to be 1.1
   by the time anything here runs (vk_create_device refuses RT otherwise), and
   keeping it local means the non-RT path is untouched. */
static PFN_vkGetPhysicalDeviceProperties2 rt_getPhysicalDeviceProperties2;

typedef struct {
	float xyz[3];
} rtVertex_t;


/*
=================
rt_create_buffer

A device-local buffer whose address can be taken.

VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT on the allocation is the part that is easy
to miss: without it vkGetBufferDeviceAddress is undefined behaviour even though
the buffer carries VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, and the failure is
a plausible-looking garbage address rather than an error.
=================
*/
static qboolean rt_create_buffer( VkDeviceSize size, VkBufferUsageFlags usage,
	VkBuffer *buffer, VkDeviceMemory *memory )
{
	VkBufferCreateInfo desc;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc_info;
	VkMemoryAllocateFlagsInfo flags_info;
	VkResult res;

	if ( size == 0 ) {
		return qfalse;
	}

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.size = size;
	desc.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	res = qvkCreateBuffer( vk.device, &desc, NULL, buffer );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT: vkCreateBuffer(%i bytes) returned %s\n",
			(int)size, vk_result_string( res ) );
		*buffer = VK_NULL_HANDLE;
		return qfalse;
	}

	qvkGetBufferMemoryRequirements( vk.device, *buffer, &reqs );

	Com_Memset( &flags_info, 0, sizeof( flags_info ) );
	flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	Com_Memset( &alloc_info, 0, sizeof( alloc_info ) );
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = &flags_info;
	alloc_info.allocationSize = reqs.size;
	alloc_info.memoryTypeIndex = find_memory_type( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	res = qvkAllocateMemory( vk.device, &alloc_info, NULL, memory );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT: vkAllocateMemory(%i bytes) returned %s\n",
			(int)reqs.size, vk_result_string( res ) );
		qvkDestroyBuffer( vk.device, *buffer, NULL );
		*buffer = VK_NULL_HANDLE;
		*memory = VK_NULL_HANDLE;
		return qfalse;
	}

	qvkBindBufferMemory( vk.device, *buffer, *memory, 0 );
	return qtrue;
}


static VkDeviceAddress rt_buffer_address( VkBuffer buffer )
{
	VkBufferDeviceAddressInfo info;

	Com_Memset( &info, 0, sizeof( info ) );
	info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	info.buffer = buffer;

	return qvkGetBufferDeviceAddressKHR( vk.device, &info );
}


/*
=================
rt_upload

Staged copy into a device-local buffer, in whatever slices the existing staging
buffer can take - the same loop vk_alloc_vbo uses, for the same reason: a world
vertex buffer is routinely larger than the staging buffer and a single
vkCmdCopyBuffer would silently truncate.
=================
*/
static void rt_upload_at( VkBuffer dst, VkDeviceSize dstOffset, const void *data, VkDeviceSize size )
{
	VkDeviceSize done = 0;

	while ( done < size ) {
		VkDeviceSize chunk = vk.staging_buffer.size;
		VkCommandBuffer cmd;
		VkBufferCopy region;

		if ( done + chunk > size ) {
			chunk = size - done;
		}
		memcpy( vk.staging_buffer.ptr, (const byte *)data + done, chunk );

		cmd = begin_command_buffer();
		region.srcOffset = 0;
		region.dstOffset = dstOffset + done;
		region.size = chunk;
		qvkCmdCopyBuffer( cmd, vk.staging_buffer.handle, dst, 1, &region );
		end_command_buffer( cmd, __func__ );

		done += chunk;
	}
}

static void rt_upload( VkBuffer dst, const void *data, VkDeviceSize size )
{
	rt_upload_at( dst, 0, data, size );
}


/*
=================
rt_surface_is_occluder

Whether a world surface should cast ambient occlusion.

Skipped, and each for a different reason:

  no shader / no data   nothing to read
  sky                   the sky is a backdrop at infinity, not a ceiling. An
                        AO structure containing it occludes the whole outdoors.
  portal / mirror       a surface you see through is not a surface light stops
                        at, and q3map already put real geometry behind it
  nodraw / non-solid    clip, hint, trigger and caulk brushes. Present in the
                        BSP, never rendered, and would occlude from inside a
                        wall where a player cannot see the cause
  translucent           glass and fog blend rather than block. Treating them as
                        opaque is a visible wrong answer; treating them as
                        absent is a small one, and it is the cheap direction.

This is the list a first pass can defend. It is deliberately conservative:
missing an occluder makes a corner slightly too bright, while including sky or
caulk makes whole rooms wrong.
=================
*/


static qboolean rt_surface_is_occluder( const msurface_t *surf )
{
	const shader_t *shader;
	int i;

	if ( surf->data == NULL || surf->shader == NULL ) {
		return qfalse;
	}
	shader = surf->shader;

	if ( shader->isSky ) {
		return qfalse;
	}
	/*
	Exactly SS_OPAQUE, not "SS_OPAQUE or below". The sort enum runs
	SS_BAD, SS_PORTAL, SS_ENVIRONMENT, SS_OPAQUE, SS_DECAL, ... so the two
	things that most need excluding - mirrors and the sky box - sort *lower*
	than opaque, and a `> SS_OPAQUE` test would have let both straight through
	while looking like it excluded them.
	*/
	if ( shader->sort != SS_OPAQUE ) {
		return qfalse;
	}
	if ( shader->surfaceFlags & ( SURF_NODRAW | SURF_SKY ) ) {
		return qfalse;
	}
	if ( shader->surfaceFlags & SURF_NONSOLID ) {
		return qfalse;
	}

	/*
	[QL] Alpha-tested surfaces: grates, fences, ladders, foliage.

	These sort SS_OPAQUE - the alpha test discards fragments rather than
	blending them, so nothing about their sort says they are full of holes - and
	they were going into the structure as solid triangles. A chain-link fence
	then occluded like a wall, and the room behind one went dark.

	Skipped rather than traced, which is the same call the translucent surfaces
	above got and for the same reason: a missing occluder makes a corner
	slightly too bright, a wrong one makes a room wrong. Light now passes
	through the solid parts of a grate as well as the holes, which is a small
	error in the forgiving direction.

	Doing it properly means tracing them as non-opaque and running the alpha
	test in an any-hit shader, which needs texture coordinates in the geometry
	buffers and every surface's texture reachable from the trace - a descriptor
	array and a second vertex stream. Worth doing; not worth pretending the
	current structure can express it.
	*/
	for ( i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *st = shader->stages[i];
		if ( st && st->active && ( st->stateBits & GLS_ATEST_BITS ) ) {
			return qfalse;
		}
	}

	return qtrue;
}


/*
=================
rt_count_surface / rt_emit_surface

Two passes over the same surfaces: count to size the buffers, then fill. Kept as
one pair of functions with a NULL-output mode rather than two walks that could
disagree - a count pass and a fill pass that drift apart by one surface type is
a buffer overrun that only happens on maps with that surface type.
=================
*/
static void rt_walk_surface( const msurface_t *surf, rtVertex_t *verts, uint32_t *indices,
	uint32_t *vertexCount, uint32_t *indexCount )
{
	const surfaceType_t *data = surf->data;
	uint32_t base = *vertexCount;
	int i;

	switch ( *data ) {

	case SF_FACE: {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)data;
		/* ofsIndices is a byte offset from the surface itself, and the indices
		   are unsigned - the same read as tr_surface.c:990, spelled the same
		   way so the two are obviously the same thing. */
		const unsigned *ind = (const unsigned *)( (const byte *)face + face->ofsIndices );

		if ( verts ) {
			for ( i = 0; i < face->numPoints; i++ ) {
				VectorCopy( face->points[i], verts[ base + i ].xyz );
			}
		}
		if ( indices ) {
			for ( i = 0; i < face->numIndices; i++ ) {
				indices[ *indexCount + i ] = base + (uint32_t)ind[i];
			}
		}
		*vertexCount += (uint32_t)face->numPoints;
		*indexCount += (uint32_t)face->numIndices;
		break;
	}

	case SF_TRIANGLES: {
		const srfTriangles_t *tri = (const srfTriangles_t *)data;

		if ( verts ) {
			for ( i = 0; i < tri->numVerts; i++ ) {
				VectorCopy( tri->verts[i].xyz, verts[ base + i ].xyz );
			}
		}
		if ( indices ) {
			for ( i = 0; i < tri->numIndexes; i++ ) {
				indices[ *indexCount + i ] = base + (uint32_t)tri->indexes[i];
			}
		}
		*vertexCount += (uint32_t)tri->numVerts;
		*indexCount += (uint32_t)tri->numIndexes;
		break;
	}

	case SF_GRID: {
		/*
		A curved patch, stored as a width x height control grid rather than as
		triangles. Two triangles per cell, taken at full grid resolution.

		Deliberately not the LOD-stitched triangulation tr_surface.c draws: that
		one varies with r_lodCurveError and the viewer's distance, and an
		occluder that changes shape as you walk toward it would make AO crawl.
		Full resolution is the stable answer and is also the most accurate one.

		Winding is not matched to the drawn surface because it cannot matter
		here - the instance is built with TRIANGLE_FACING_CULL_DISABLE, so a
		trace hits a patch from either side, which is what an occluder should do.
		*/
		const srfGridMesh_t *grid = (const srfGridMesh_t *)data;
		int x, y;

		if ( verts ) {
			for ( i = 0; i < grid->width * grid->height; i++ ) {
				VectorCopy( grid->verts[i].xyz, verts[ base + i ].xyz );
			}
		}
		if ( indices ) {
			uint32_t n = *indexCount;

			for ( y = 0; y < grid->height - 1; y++ ) {
				for ( x = 0; x < grid->width - 1; x++ ) {
					uint32_t v0 = base + (uint32_t)( y * grid->width + x );
					uint32_t v1 = v0 + 1;
					uint32_t v2 = base + (uint32_t)( ( y + 1 ) * grid->width + x );
					uint32_t v3 = v2 + 1;

					indices[ n++ ] = v0;
					indices[ n++ ] = v2;
					indices[ n++ ] = v1;

					indices[ n++ ] = v1;
					indices[ n++ ] = v2;
					indices[ n++ ] = v3;
				}
			}
		}
		/* Counted the same way whether or not anything was written, so the
		   fill passes cannot drift from the counting pass. */
		*indexCount += (uint32_t)( ( grid->width - 1 ) * ( grid->height - 1 ) * 6 );
		*vertexCount += (uint32_t)( grid->width * grid->height );
		break;
	}

	default:
		/* SF_BAD, SF_SKIP, SF_FLARE, SF_ENTITY, SF_DISPLAY_LIST and the model
		   types. None of them is static world geometry. */
		break;
	}
}


/*
=================
rt_scratch_alignment

minAccelerationStructureScratchOffsetAlignment, which has no default worth
guessing: it is 128 on some drivers and 256 on others, and an underaligned
scratch address is undefined behaviour rather than an error return.
=================
*/
static VkDeviceSize rt_scratch_alignment( void )
{
	VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props;
	VkPhysicalDeviceProperties2 props2;

	if ( rt_getPhysicalDeviceProperties2 == NULL ) {
		return 256;  // the larger of the two values seen in the wild
	}

	Com_Memset( &as_props, 0, sizeof( as_props ) );
	Com_Memset( &props2, 0, sizeof( props2 ) );
	as_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props2.pNext = &as_props;

	rt_getPhysicalDeviceProperties2( vk.physical_device, &props2 );

	if ( as_props.minAccelerationStructureScratchOffsetAlignment == 0 ) {
		return 256;
	}
	return as_props.minAccelerationStructureScratchOffsetAlignment;
}


/*
=================
rt_build_acceleration_structure

Shared tail of the BLAS and TLAS builds: size the structure, create its backing
buffer, create the structure, allocate scratch, record the build, wait.

One command buffer per structure and a full wait after each. This is map-load
work that happens twice, so the simplicity is worth more than the overlap.
=================
*/
static qboolean rt_build_acceleration_structure(
	VkAccelerationStructureBuildGeometryInfoKHR *build_info,
	uint32_t primitiveCount,
	VkAccelerationStructureTypeKHR type,
	VkAccelerationStructureKHR *as,
	VkBuffer *as_buffer,
	VkDeviceMemory *as_memory,
	const char *what )
{
	VkAccelerationStructureBuildSizesInfoKHR sizes;
	VkAccelerationStructureCreateInfoKHR create_info;
	VkAccelerationStructureBuildRangeInfoKHR range;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges[1];
	VkBuffer scratch_buffer = VK_NULL_HANDLE;
	VkDeviceMemory scratch_memory = VK_NULL_HANDLE;
	VkDeviceSize scratchAlign;
	VkCommandBuffer cmd;
	VkMemoryBarrier barrier;
	VkResult res;

	Com_Memset( &sizes, 0, sizeof( sizes ) );
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	qvkGetAccelerationStructureBuildSizesKHR( vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, build_info, &primitiveCount, &sizes );

	if ( sizes.accelerationStructureSize == 0 ) {
		ri.Printf( PRINT_WARNING, "RT: %s sized to zero bytes\n", what );
		return qfalse;
	}

	if ( !rt_create_buffer( sizes.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, as_buffer, as_memory ) ) {
		return qfalse;
	}

	Com_Memset( &create_info, 0, sizeof( create_info ) );
	create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	create_info.buffer = *as_buffer;
	create_info.offset = 0;
	create_info.size = sizes.accelerationStructureSize;
	create_info.type = type;

	res = qvkCreateAccelerationStructureKHR( vk.device, &create_info, NULL, as );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "RT: vkCreateAccelerationStructureKHR(%s) returned %s\n",
			what, vk_result_string( res ) );
		*as = VK_NULL_HANDLE;
		return qfalse;
	}

	/* Over-allocate by the alignment so the address can be rounded up into the
	   buffer rather than hoping the allocator already returned an aligned one. */
	scratchAlign = rt_scratch_alignment();
	if ( !rt_create_buffer( sizes.buildScratchSize + scratchAlign,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &scratch_buffer, &scratch_memory ) ) {
		qvkDestroyAccelerationStructureKHR( vk.device, *as, NULL );
		*as = VK_NULL_HANDLE;
		return qfalse;
	}

	build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info->dstAccelerationStructure = *as;
	build_info->scratchData.deviceAddress =
		( rt_buffer_address( scratch_buffer ) + scratchAlign - 1 ) & ~( scratchAlign - 1 );

	Com_Memset( &range, 0, sizeof( range ) );
	range.primitiveCount = primitiveCount;
	ranges[0] = &range;

	cmd = begin_command_buffer();
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, build_info, ranges );

	/*
	The TLAS build reads the BLAS this barrier follows, and the AO pass will
	read the TLAS. VK_ACCESS_ACCELERATION_STRUCTURE_WRITE/READ is the pair that
	covers both, and without it the second build can begin before the first has
	landed - on a driver that overlaps them, which is not the one you test on.
	*/
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier( cmd,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, NULL, 0, NULL );

	end_command_buffer( cmd, what );

	/* end_command_buffer waits for the queue, so the scratch is finished with
	   by the time we get here and can go back immediately - it is the largest
	   allocation in the whole build and holding it for the map would be pure
	   waste. */
	qvkDestroyBuffer( vk.device, scratch_buffer, NULL );
	qvkFreeMemory( vk.device, scratch_memory, NULL );

	ri.Printf( PRINT_DEVELOPER, "RT: %s built, %i primitives, %i KiB\n",
		what, (int)primitiveCount, (int)( sizes.accelerationStructureSize / 1024 ) );
	return qtrue;
}




static qboolean rtDynReported = qfalse;   // [QL] instance count, once per map
/*
[QL] And once per map again, the first time a round proxy actually appears.

The line above fires on the first frame the structure is built, which is before
anybody has fired anything, so its round count is always 0 and it can never
answer the question it looks like it answers. Nothing round exists at map load:
projectiles, gibs and brass are the only things that carry RF_OCCLUDE_ROUND and
all three are made by shooting. This is the one that says whether they reach the
structure at all.
*/
static qboolean rtDynRoundReported = qfalse;


/* [QL] E161, defined below; vk_rt_build_world calls it. */
static void vk_rt_update_main_descriptor( void );



/*
[QL] E161: binding 1 of every command buffer's uniform set names this map's
static structure, for the shadow rays in the main pass.

The static one, not the per-frame one with the entities in it: that is rebuilt
after the main pass, in the same command buffer, so the main pass would read a
structure a frame or two old that a later command then rewrites under it. The
level is what these shadows are for, and the static structure is exactly the
level - fixed for the whole map, nothing to synchronise.
*/
static void vk_rt_update_main_descriptor( void )
{
	uint32_t n;

	vk.rt.world.mainTlasWritten = qfalse;
	if ( !vk.rtActive || vk.rt.world.tlas == VK_NULL_HANDLE ) {
		return;
	}

	for ( n = 0; n < NUM_COMMAND_BUFFERS; n++ ) {
		VkWriteDescriptorSetAccelerationStructureKHR as_info;
		VkWriteDescriptorSet write;
		/* [QL] E165: the per-frame structure (level, doors and lifts, players
		   and items) when there is one - vk_rt_prebuild_dynamic builds it
		   before the main pass reads it - else the static level */
		VkAccelerationStructureKHR as = ( vk.rt.world.dynReady && vk.rt.world.dyn_tlas[n] != VK_NULL_HANDLE )
			? vk.rt.world.dyn_tlas[n] : vk.rt.world.tlas;

		if ( vk.tess[n].uniform_descriptor == VK_NULL_HANDLE ) {
			return;
		}
		Com_Memset( &as_info, 0, sizeof( as_info ) );
		as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		as_info.accelerationStructureCount = 1;
		as_info.pAccelerationStructures = &as;

		Com_Memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.pNext = &as_info;
		write.dstSet = vk.tess[n].uniform_descriptor;
		write.dstBinding = 1;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
	vk.rt.world.mainTlasWritten = qtrue;
	ri.Printf( PRINT_ALL, "RT shadows: level structure bound - model and dynamic light shadows can run "
		"(r_rtModelShadows %i, r_rtDlightShadows %i)\n", r_rtModelShadows->integer, r_rtDlightShadows->integer );
}


/* [QL] R13 step 4: the dynamic half. One instance of the world, plus a proxy
   per visible entity, in a top level structure rebuilt every frame. */
#define RT_MAX_DYN_INSTANCES 256

/* The ball proxy's tessellation. Vertices are the two poles plus each
   intermediate ring; triangles are a fan at each pole plus two per quad in
   between. */
#define RT_BALL_SEGMENTS 12
#define RT_BALL_RINGS 6
#define RT_BALL_VERTS ( 2 + ( RT_BALL_RINGS - 1 ) * RT_BALL_SEGMENTS )
#define RT_BALL_TRIS ( 2 * RT_BALL_SEGMENTS + ( RT_BALL_RINGS - 2 ) * RT_BALL_SEGMENTS * 2 )

/*
=================
rt_create_host_buffer

Like rt_create_buffer but in memory the CPU can write, and left mapped.

The instance array is rewritten every frame from the entity list, so it wants
host-visible memory and one persistent mapping rather than a staging copy and a
transfer per frame for a few kilobytes.
=================
*/
qboolean rt_create_host_buffer( VkDeviceSize size, VkBufferUsageFlags usage,
	VkBuffer *buffer, VkDeviceMemory *memory, void **mapped )
{
	VkBufferCreateInfo desc;
	VkMemoryAllocateInfo alloc_info;
	VkMemoryAllocateFlagsInfo flags_info;
	VkMemoryRequirements reqs;
	VkResult res;

	*buffer = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
	*mapped = NULL;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.size = size;
	/* [QL] E153: an address only when ray query enabled bufferDeviceAddress -
	   the water pass's uniform buffer comes through here and runs without it */
	desc.usage = usage | ( vk.rtActive ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0 );
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	res = qvkCreateBuffer( vk.device, &desc, NULL, buffer );
	if ( res < 0 ) {
		*buffer = VK_NULL_HANDLE;
		return qfalse;
	}

	qvkGetBufferMemoryRequirements( vk.device, *buffer, &reqs );

	Com_Memset( &flags_info, 0, sizeof( flags_info ) );
	flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	Com_Memset( &alloc_info, 0, sizeof( alloc_info ) );
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = vk.rtActive ? &flags_info : NULL;
	alloc_info.allocationSize = reqs.size;
	alloc_info.memoryTypeIndex = find_memory_type( reqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	res = qvkAllocateMemory( vk.device, &alloc_info, NULL, memory );
	if ( res < 0 ) {
		qvkDestroyBuffer( vk.device, *buffer, NULL );
		*buffer = VK_NULL_HANDLE;
		*memory = VK_NULL_HANDLE;
		return qfalse;
	}

	qvkBindBufferMemory( vk.device, *buffer, *memory, 0 );

	res = qvkMapMemory( vk.device, *memory, 0, VK_WHOLE_SIZE, 0, mapped );
	if ( res < 0 ) {
		qvkFreeMemory( vk.device, *memory, NULL );
		qvkDestroyBuffer( vk.device, *buffer, NULL );
		*buffer = VK_NULL_HANDLE;
		*memory = VK_NULL_HANDLE;
		*mapped = NULL;
		return qfalse;
	}

	return qtrue;
}


static void rt_destroy_proxy( vk_rt_proxy_t *proxy )
{
	if ( proxy->blas != VK_NULL_HANDLE ) {
		qvkDestroyAccelerationStructureKHR( vk.device, proxy->blas, NULL );
		proxy->blas = VK_NULL_HANDLE;
	}
	if ( proxy->blas_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, proxy->blas_buffer, NULL );
		qvkFreeMemory( vk.device, proxy->blas_memory, NULL );
		proxy->blas_buffer = VK_NULL_HANDLE;
		proxy->blas_memory = VK_NULL_HANDLE;
	}
	if ( proxy->vertex_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, proxy->vertex_buffer, NULL );
		qvkFreeMemory( vk.device, proxy->vertex_memory, NULL );
		proxy->vertex_buffer = VK_NULL_HANDLE;
		proxy->vertex_memory = VK_NULL_HANDLE;
	}
	if ( proxy->index_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, proxy->index_buffer, NULL );
		qvkFreeMemory( vk.device, proxy->index_memory, NULL );
		proxy->index_buffer = VK_NULL_HANDLE;
		proxy->index_memory = VK_NULL_HANDLE;
	}
}


/*
=================
rt_proxy_winding_is_outward

Every triangle of a proxy must have its normal pointing away from the centre.

This is checked rather than assumed because getting it wrong is invisible.
The occlusion trace culls back faces on these instances, so the winding alone
decides whether a proxy occludes the world around it or only occludes from
inside itself - and both look like "a plausible render with the ambient
occlusion slightly off". A reversed triple in the table below produced exactly
that once already, and it took a matched pair of screenshots to find, because
nothing in the build or the validation layers has any opinion about it.

Both proxies are convex and centred on the origin, so the test is the cheap one
it looks like: for a triangle of an outward-wound convex hull about the origin,
the cross product of its edges points the same way as the vector from the origin
to the triangle. Sum over the mesh and the sign is the whole answer.
=================
*/
static qboolean rt_proxy_winding_is_outward( const float ( *verts )[3],
	const uint32_t *indices, uint32_t numTriangles )
{
	uint32_t t, j;

	for ( t = 0; t < numTriangles; t++ ) {
		const float *a = verts[ indices[ t * 3 + 0 ] ];
		const float *b = verts[ indices[ t * 3 + 1 ] ];
		const float *c = verts[ indices[ t * 3 + 2 ] ];
		vec3_t ab, ac, n, centroid;

		for ( j = 0; j < 3; j++ ) {
			ab[j] = b[j] - a[j];
			ac[j] = c[j] - a[j];
			centroid[j] = ( a[j] + b[j] + c[j] ) * ( 1.0f / 3.0f );
		}
		CrossProduct( ab, ac, n );

		if ( DotProduct( n, centroid ) <= 0.0f ) {
			ri.Printf( PRINT_WARNING, "RT: proxy triangle %i is wound inward\n", (int)t );
			return qfalse;
		}
	}

	return qtrue;
}


/*
=================
rt_build_ball_mesh

The other proxy: a unit sphere, generated rather than tabulated.

Segments and rings are deliberately low. This stands in for a rocket or a gib
at the far end of a soft, blurred, distance-weighted occlusion term - the
difference between 120 facets and a real sphere is not expressible in the
output, and the whole point of a shared proxy is that it is built once and
costs nothing per frame. It is generated because a hand-written table of 62
vertices is 62 chances to transpose a sign, and because every triangle here has
to be wound outward for the same reason the box does.

Diameter 1, matching the box, so the instance transform that scales the box to
an entity's bounds scales this to the ellipsoid inscribed in those same bounds
with no separate arithmetic at the call site.
=================
*/
static void rt_build_ball_mesh( float ( *verts )[3], uint32_t *indices )
{
	const uint32_t north = 0;
	const uint32_t south = RT_BALL_VERTS - 1;
	uint32_t ring, seg, v, n;

	/* poles, then each intermediate ring from the top down */
	VectorSet( verts[north], 0.0f, 0.0f, 0.5f );
	VectorSet( verts[south], 0.0f, 0.0f, -0.5f );

	v = 1;
	for ( ring = 1; ring < RT_BALL_RINGS; ring++ ) {
		const float theta = (float)M_PI * (float)ring / (float)RT_BALL_RINGS;
		const float z = cosf( theta ) * 0.5f;
		const float r = sinf( theta ) * 0.5f;

		for ( seg = 0; seg < RT_BALL_SEGMENTS; seg++ ) {
			const float phi = 2.0f * (float)M_PI * (float)seg / (float)RT_BALL_SEGMENTS;
			VectorSet( verts[v], r * cosf( phi ), r * sinf( phi ), z );
			v++;
		}
	}

	/*
	Ring r's segment s, for r in 1 .. RINGS-1. The poles are not in the rings,
	hence the -1 on the ring index and the +1 for the north pole ahead of them.
	*/
#define BALL_V( r, s ) ( 1 + ( ( (r) - 1 ) * RT_BALL_SEGMENTS ) + ( (s) % RT_BALL_SEGMENTS ) )

	n = 0;

	/* top cap: pole, then the two ring vertices in increasing segment order */
	for ( seg = 0; seg < RT_BALL_SEGMENTS; seg++ ) {
		indices[n++] = north;
		indices[n++] = BALL_V( 1, seg );
		indices[n++] = BALL_V( 1, seg + 1 );
	}

	/* the bands between rings, two triangles per quad, same handedness */
	for ( ring = 1; ring + 1 < RT_BALL_RINGS; ring++ ) {
		for ( seg = 0; seg < RT_BALL_SEGMENTS; seg++ ) {
			indices[n++] = BALL_V( ring, seg );
			indices[n++] = BALL_V( ring + 1, seg );
			indices[n++] = BALL_V( ring + 1, seg + 1 );

			indices[n++] = BALL_V( ring, seg );
			indices[n++] = BALL_V( ring + 1, seg + 1 );
			indices[n++] = BALL_V( ring, seg + 1 );
		}
	}

	/* bottom cap: the mirror of the top, so the segment order reverses */
	for ( seg = 0; seg < RT_BALL_SEGMENTS; seg++ ) {
		indices[n++] = south;
		indices[n++] = BALL_V( RT_BALL_RINGS - 1, seg + 1 );
		indices[n++] = BALL_V( RT_BALL_RINGS - 1, seg );
	}

#undef BALL_V
}


/*
=================
rt_create_proxy

Upload one proxy mesh and build a bottom level structure over it.
=================
*/
static qboolean rt_create_proxy( vk_rt_proxy_t *proxy, const float ( *verts )[3],
	uint32_t numVertices, const uint32_t *indices, uint32_t numTriangles,
	const char *name )
{
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	const VkDeviceSize vertexSize = (VkDeviceSize)numVertices * sizeof( float ) * 3;
	const VkDeviceSize indexSize = (VkDeviceSize)numTriangles * 3 * sizeof( uint32_t );

	if ( !rt_proxy_winding_is_outward( verts, indices, numTriangles ) ) {
		ri.Printf( PRINT_WARNING, "RT: %s has inward faces, dynamic occlusion disabled\n", name );
		return qfalse;
	}

	if ( !rt_create_buffer( vertexSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			&proxy->vertex_buffer, &proxy->vertex_memory ) ) {
		return qfalse;
	}
	rt_upload( proxy->vertex_buffer, verts, vertexSize );

	if ( !rt_create_buffer( indexSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			&proxy->index_buffer, &proxy->index_memory ) ) {
		return qfalse;
	}
	rt_upload( proxy->index_buffer, indices, indexSize );

	Com_Memset( &geom, 0, sizeof( geom ) );
	geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geom.geometry.triangles.vertexData.deviceAddress = rt_buffer_address( proxy->vertex_buffer );
	geom.geometry.triangles.vertexStride = sizeof( float ) * 3;
	geom.geometry.triangles.maxVertex = numVertices - 1;
	geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geom.geometry.triangles.indexData.deviceAddress = rt_buffer_address( proxy->index_buffer );

	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;

	return rt_build_acceleration_structure( &build_info, numTriangles,
		VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		&proxy->blas, &proxy->blas_buffer, &proxy->blas_memory, name );
}


static void rt_caps_clear( void );

static void vk_rt_destroy_dynamic( void )
{
	uint32_t i;

	rt_caps_clear();   /* [QL] E180: the models' holes go with the map */

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.rt.world.dyn_tlas[i] != VK_NULL_HANDLE ) {
			qvkDestroyAccelerationStructureKHR( vk.device, vk.rt.world.dyn_tlas[i], NULL );
			vk.rt.world.dyn_tlas[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.world.dyn_tlas_buffer[i] != VK_NULL_HANDLE ) {
			qvkDestroyBuffer( vk.device, vk.rt.world.dyn_tlas_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.dyn_tlas_memory[i], NULL );
			vk.rt.world.dyn_tlas_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.dyn_tlas_memory[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.world.dyn_instance_buffer[i] != VK_NULL_HANDLE ) {
			qvkUnmapMemory( vk.device, vk.rt.world.dyn_instance_memory[i] );
			qvkDestroyBuffer( vk.device, vk.rt.world.dyn_instance_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.dyn_instance_memory[i], NULL );
			vk.rt.world.dyn_instance_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.dyn_instance_memory[i] = VK_NULL_HANDLE;
			vk.rt.world.dyn_instance_ptr[i] = NULL;
		}
		if ( vk.rt.world.dyn_scratch_buffer[i] != VK_NULL_HANDLE ) {
			qvkDestroyBuffer( vk.device, vk.rt.world.dyn_scratch_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.dyn_scratch_memory[i], NULL );
			vk.rt.world.dyn_scratch_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.dyn_scratch_memory[i] = VK_NULL_HANDLE;
			vk.rt.world.dyn_scratch_address[i] = 0;
		}
	}

	rt_destroy_proxy( &vk.rt.world.proxy_box );
	rt_destroy_proxy( &vk.rt.world.proxy_ball );

	/* [QL] E166: the actor meshes */
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.rt.world.actor_blas[i] != VK_NULL_HANDLE ) {
			qvkDestroyAccelerationStructureKHR( vk.device, vk.rt.world.actor_blas[i], NULL );
			vk.rt.world.actor_blas[i] = VK_NULL_HANDLE;
			vk.rt.world.actorBuiltValid[i] = qfalse;   /* [QL] E205 */
		}
		if ( vk.rt.world.actor_blas_buffer[i] != VK_NULL_HANDLE ) {
			qvkDestroyBuffer( vk.device, vk.rt.world.actor_blas_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.actor_blas_memory[i], NULL );
			vk.rt.world.actor_blas_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_blas_memory[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.world.actor_scratch_buffer[i] != VK_NULL_HANDLE ) {
			qvkDestroyBuffer( vk.device, vk.rt.world.actor_scratch_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.actor_scratch_memory[i], NULL );
			vk.rt.world.actor_scratch_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_scratch_memory[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.world.actor_vertex_buffer[i] != VK_NULL_HANDLE ) {
			qvkUnmapMemory( vk.device, vk.rt.world.actor_vertex_memory[i] );
			qvkDestroyBuffer( vk.device, vk.rt.world.actor_vertex_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.actor_vertex_memory[i], NULL );
			vk.rt.world.actor_vertex_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_vertex_memory[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_vertex_ptr[i] = NULL;
		}
		if ( vk.rt.world.actor_index_buffer[i] != VK_NULL_HANDLE ) {
			qvkUnmapMemory( vk.device, vk.rt.world.actor_index_memory[i] );
			qvkDestroyBuffer( vk.device, vk.rt.world.actor_index_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.rt.world.actor_index_memory[i], NULL );
			vk.rt.world.actor_index_buffer[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_index_memory[i] = VK_NULL_HANDLE;
			vk.rt.world.actor_index_ptr[i] = NULL;
		}
	}
	vk.rt.world.actorReady = qfalse;

	vk.rt.world.dynReady = qfalse;
	vk.rt.world.dyn_maxInstances = 0;
}


/*
=================
rt_create_actor_mesh / rt_build_actor_mesh

[QL] E166. Players and items as their real triangles, for the traced shadows
they cast (r_rtActorShadows, and r_rtShadowCasters 1).

The proxy boxes the occlusion pass uses are a player's bounds, not a player:
fine for darkening the floor under someone, useless as a shadow, which is all
silhouette. So each frame the visible MD3 models are lerped on the CPU - the
same lerp RB_SurfaceMesh draws them with, so the shadow matches the frame on
screen - put into the world, and built into one bottom-level structure per
command buffer, sized once for the worst case.

Your own body is in it (RF_THIRD_PERSON: not drawn in first person, but it
casts); the view weapon is not (RF_FIRST_PERSON, drawn at the eye).
=================
*/
#define RT_ACTOR_MAX_VERTS	( 192 * 1024 )
#define RT_ACTOR_MAX_TRIS	( 256 * 1024 )

static void rt_actor_geometry( VkAccelerationStructureGeometryKHR *geom, int idx, uint32_t numVerts )
{
	Com_Memset( geom, 0, sizeof( *geom ) );
	geom->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geom->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geom->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geom->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geom->geometry.triangles.vertexStride = sizeof( float ) * 3;
	geom->geometry.triangles.maxVertex = numVerts > 0 ? numVerts - 1 : 0;
	geom->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	if ( idx >= 0 ) {
		geom->geometry.triangles.vertexData.deviceAddress = rt_buffer_address( vk.rt.world.actor_vertex_buffer[idx] );
		geom->geometry.triangles.indexData.deviceAddress = rt_buffer_address( vk.rt.world.actor_index_buffer[idx] );
	}
}

static void rt_create_actor_mesh( void )
{
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	VkAccelerationStructureBuildSizesInfoKHR sizes;
	VkAccelerationStructureCreateInfoKHR create_info;
	const uint32_t maxTris = RT_ACTOR_MAX_TRIS;
	VkDeviceSize scratchAlign;
	uint32_t i;

	vk.rt.world.actorReady = qfalse;

	rt_actor_geometry( &geom, -1, RT_ACTOR_MAX_VERTS );
	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
		VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;   /* [QL] E205: refit when the set is unchanged */
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;

	Com_Memset( &sizes, 0, sizeof( sizes ) );
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	qvkGetAccelerationStructureBuildSizesKHR( vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &maxTris, &sizes );
	if ( sizes.accelerationStructureSize == 0 ) {
		goto fail;
	}
	scratchAlign = rt_scratch_alignment();

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( !rt_create_host_buffer( (VkDeviceSize)RT_ACTOR_MAX_VERTS * sizeof( float ) * 3,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
				&vk.rt.world.actor_vertex_buffer[i], &vk.rt.world.actor_vertex_memory[i],
				&vk.rt.world.actor_vertex_ptr[i] ) ) {
			goto fail;
		}
		if ( !rt_create_host_buffer( (VkDeviceSize)RT_ACTOR_MAX_TRIS * 3 * sizeof( uint32_t ),
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
				&vk.rt.world.actor_index_buffer[i], &vk.rt.world.actor_index_memory[i],
				&vk.rt.world.actor_index_ptr[i] ) ) {
			goto fail;
		}
		if ( !rt_create_buffer( sizes.accelerationStructureSize,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
				&vk.rt.world.actor_blas_buffer[i], &vk.rt.world.actor_blas_memory[i] ) ) {
			goto fail;
		}
		Com_Memset( &create_info, 0, sizeof( create_info ) );
		create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		create_info.buffer = vk.rt.world.actor_blas_buffer[i];
		create_info.size = sizes.accelerationStructureSize;
		create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		if ( qvkCreateAccelerationStructureKHR( vk.device, &create_info, NULL, &vk.rt.world.actor_blas[i] ) < 0 ) {
			vk.rt.world.actor_blas[i] = VK_NULL_HANDLE;
			goto fail;
		}
		if ( !rt_create_buffer( ( sizes.buildScratchSize > sizes.updateScratchSize ? sizes.buildScratchSize : sizes.updateScratchSize ) + scratchAlign,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				&vk.rt.world.actor_scratch_buffer[i], &vk.rt.world.actor_scratch_memory[i] ) ) {
			goto fail;
		}
		vk.rt.world.actor_scratch_address[i] =
			( rt_buffer_address( vk.rt.world.actor_scratch_buffer[i] ) + scratchAlign - 1 ) & ~( scratchAlign - 1 );
	}

	vk.rt.world.actorReady = qtrue;
	ri.Printf( PRINT_ALL, "RT: player/item silhouettes ready (up to %i triangles a frame, %i KiB structure)\n",
		RT_ACTOR_MAX_TRIS, (int)( sizes.accelerationStructureSize / 1024 ) );
	return;

fail:
	ri.Printf( PRINT_WARNING, "RT: could not create the player/item silhouette structures - "
		"players and items will cast no traced shadows\n" );
	/* the handles made so far are freed with the rest in vk_rt_destroy_dynamic */
	vk.rt.world.actorReady = qfalse;
}

/* is anything this frame going to trace the silhouettes */
static qboolean rt_actors_wanted( void )
{
	return ( rtf.actorMesh || r_rtDynamic->integer == 2 ) ? qtrue : qfalse;   /* [QL] E174: AO by real shapes */
}

/*
=================
rt_caps_for

[QL] E180: the holes in a model, closed for its shadow.

A Quake player is three MD3 models - legs, torso, head - each an open shell,
with a hole where it meets the next: the waist, the neck, the top of the legs.
Weapons and many items have open ends the same way. Drawn, the holes never
show. Traced from the floor toward a light high overhead, a ray under the
player goes straight up the inside of the legs, out through the neck and on to
the light without touching a triangle, so the middle of the shadow is lit and
only the rim, where rays graze the walls, is dark - the "hollow" shadows in the
tester's screenshots (an outline on the floor with the floor showing through).

Found once per model and kept: vertices welded by their position in frame 0,
across all of the model's surfaces, every edge used by one triangle only is on
a hole, and the edges are grouped into holes by which welded vertices they
share. Each frame, a fan from the hole's current centre closes it in the
structure - the same lerped positions as the rest of the model, so the cap
moves with it.

[QL] E184: per model, not per surface. A player's torso is several surfaces
stitched together, and E180 welded and capped each one on its own: a surface's
"hole" was then its whole outline, seams to the next surface included, and a
fan from the centre of a half-ring like that covers most of the opening but
leaves a lens down the middle. Light went through the lens, and the shadows
stayed hollow on the tester's player models - while E180's test, one tube
made of one surface, came out closed. Welded across surfaces, a seam is two
triangles sharing an edge like any other, and only the real holes - waist,
neck, open ends - are left to close. Checked with a tube split into two
surfaces along two seams.

The table is cleared with the dynamic structures, which is every map load, so
a model unloaded with the map cannot leave its answer under a pointer a new
one is given.
=================
*/
#define RT_CAP_SLOTS	2048
#define RT_CAP_LOOPS	32

typedef struct {
	const md3Header_t	*model;
	int					numEdges;
	int					numLoops;
	int					*edges;		/* numEdges * 3: from, to, hole */
} rtCapInfo_t;

static rtCapInfo_t rtCaps[RT_CAP_SLOTS];

static void rt_caps_clear( void )
{
	int i;
	for ( i = 0; i < RT_CAP_SLOTS; i++ ) {
		if ( rtCaps[i].edges ) {
			ri.Free( rtCaps[i].edges );
		}
	}
	Com_Memset( rtCaps, 0, sizeof( rtCaps ) );
}

/*
[QL] E188: called by RE_Shutdown just before ri.FreeAll().

The table is a static and outlives a renderer restart - every map change is
RE_Shutdown( REF_KEEP_CONTEXT ) then R_Init, with the DLL kept loaded - but
its edge lists are zone memory tagged TAG_RENDERER, which ri.FreeAll() frees
wholesale at the end of RE_Shutdown. The table then held pointers into freed
memory, and the next map's vk_rt_destroy_dynamic handed them to Z_Free: the
tester's crash, "Z_Free: freed a pointer without ZONEID", on the second map
load of a session (exec ffa, devmap). The harness loads one map and never
saw it. Emptying the table here, while the memory is still ours to free,
leaves nothing behind.
*/
void vk_rt_caps_reset( void )
{
	rt_caps_clear();
}

static int rt_cap_edge_cmp( const void *a, const void *b )
{
	const int *x = (const int *)a, *y = (const int *)b;
	if ( x[0] != y[0] ) return x[0] - y[0];
	return x[1] - y[1];
}

static int rt_cap_find( int *parent, int v )
{
	while ( parent[v] != v ) {
		parent[v] = parent[parent[v]];
		v = parent[v];
	}
	return v;
}

static const short *rtCapWeldXyz;

static int rt_cap_weld_cmp( const void *a, const void *b )
{
	const short *p = rtCapWeldXyz + *(const int *)a * 4, *q = rtCapWeldXyz + *(const int *)b * 4;
	if ( p[0] != q[0] ) return p[0] - q[0];
	if ( p[1] != q[1] ) return p[1] - q[1];
	if ( p[2] != q[2] ) return p[2] - q[2];
	return *(const int *)a - *(const int *)b;
}

static const rtCapInfo_t *rt_caps_for( const md3Header_t *model )
{
	const uintptr_t key = (uintptr_t)model;
	const md3Surface_t *surf;
	rtCapInfo_t *c = NULL;
	short *xyz;
	int *weld, *und, *parent, *loopId, *order;
	int i, j, k, slot, nb = 0, nv = 0, ne = 0, base;

	slot = (int)( ( key >> 4 ) * 2654435761u % RT_CAP_SLOTS );
	for ( i = 0; i < RT_CAP_SLOTS; i++ ) {
		rtCapInfo_t *s = &rtCaps[ ( slot + i ) % RT_CAP_SLOTS ];
		if ( s->model == model ) {
			return s;
		}
		if ( s->model == NULL ) {
			c = s;
			break;
		}
	}
	if ( c == NULL ) {
		return NULL;   /* table full: this model goes uncapped */
	}
	surf = (const md3Surface_t *)( (const byte *)model + model->ofsSurfaces );
	for ( i = 0; i < model->numSurfaces; i++ ) {
		nv += surf->numVerts;
		ne += surf->numTriangles * 3;
		surf = (const md3Surface_t *)( (const byte *)surf + surf->ofsEnd );
	}
	c->model = model;
	if ( nv <= 0 || ne <= 0 ) {
		return c;
	}

	/* every surface's frame-0 positions, in the order rt_build_actor_mesh
	   writes the vertices, so an index here is an offset from the model's
	   first vertex there */
	xyz = ri.Malloc( nv * 4 * sizeof( short ) );
	weld = ri.Malloc( nv * sizeof( int ) );
	order = ri.Malloc( nv * sizeof( int ) );
	parent = ri.Malloc( nv * sizeof( int ) );
	loopId = ri.Malloc( nv * sizeof( int ) );
	und = ri.Malloc( ne * 4 * sizeof( int ) );
	surf = (const md3Surface_t *)( (const byte *)model + model->ofsSurfaces );
	for ( i = 0, base = 0, k = 0; i < model->numSurfaces; i++ ) {
		const int *tris = (const int *)( (const byte *)surf + surf->ofsTriangles );
		Com_Memcpy( xyz + base * 4, (const byte *)surf + surf->ofsXyzNormals, surf->numVerts * 4 * sizeof( short ) );
		for ( j = 0; j < surf->numTriangles * 3; j++ ) {
			und[k * 4 + 2] = base + tris[j];
			und[k * 4 + 3] = base + tris[ ( j % 3 == 2 ) ? j - 2 : j + 1 ];
			k++;
		}
		base += surf->numVerts;
		surf = (const md3Surface_t *)( (const byte *)surf + surf->ofsEnd );
	}
	/* weld: sort by position, the first of each run stands for the rest */
	for ( i = 0; i < nv; i++ ) {
		order[i] = i;
		parent[i] = i;
		loopId[i] = -1;
	}
	rtCapWeldXyz = xyz;
	qsort( order, nv, sizeof( int ), rt_cap_weld_cmp );
	for ( i = 0; i < nv; i++ ) {
		const short *p = xyz + order[i] * 4;
		if ( i > 0 ) {
			const short *q = xyz + order[i - 1] * 4;
			if ( p[0] == q[0] && p[1] == q[1] && p[2] == q[2] ) {
				weld[ order[i] ] = weld[ order[i - 1] ];
				continue;
			}
		}
		weld[ order[i] ] = order[i];
	}
	/* every edge, undirected for counting, with its direction kept */
	for ( i = 0; i < ne; i++ ) {
		const int a = weld[ und[i * 4 + 2] ], b = weld[ und[i * 4 + 3] ];
		und[i*4+0] = a < b ? a : b;
		und[i*4+1] = a < b ? b : a;
		und[i*4+2] = a;
		und[i*4+3] = b;
	}
	qsort( und, ne, 4 * sizeof( int ), rt_cap_edge_cmp );
	/* used once: on a hole. Kept in place at the front of und. */
	for ( i = 0; i < ne; i = j ) {
		for ( j = i + 1; j < ne && und[j*4] == und[i*4] && und[j*4+1] == und[i*4+1]; j++ ) {}
		if ( j - i == 1 && und[i*4] != und[i*4+1] ) {
			for ( k = 0; k < 4; k++ ) und[nb*4+k] = und[i*4+k];
			nb++;
		}
	}
	for ( i = 0; i < nb; i++ ) {
		const int ra = rt_cap_find( parent, und[i*4+2] ), rb = rt_cap_find( parent, und[i*4+3] );
		if ( ra != rb ) parent[ra] = rb;
	}
	if ( nb >= 3 ) {
		c->edges = ri.Malloc( nb * 3 * sizeof( int ) );
		for ( i = 0; i < nb; i++ ) {
			const int r = rt_cap_find( parent, und[i*4+2] );
			if ( loopId[r] < 0 ) {
				loopId[r] = c->numLoops < RT_CAP_LOOPS ? c->numLoops++ : RT_CAP_LOOPS;
			}
			if ( loopId[r] >= RT_CAP_LOOPS ) {
				continue;   /* more holes than a model plausibly has: leave the rest */
			}
			c->edges[c->numEdges*3+0] = und[i*4+2];
			c->edges[c->numEdges*3+1] = und[i*4+3];
			c->edges[c->numEdges*3+2] = loopId[r];
			c->numEdges++;
		}
	}
	ri.Free( xyz );
	ri.Free( weld );
	ri.Free( order );
	ri.Free( parent );
	ri.Free( loopId );
	ri.Free( und );
	return c;
}

/*
Fill this command buffer's actor mesh and record its build. Returns the
triangle count; 0 means nothing was built and the instance must be left out.
*/
/*
=================
rt_in_reach

[QL] E203: could anything this entity does in the traced passes show on screen?

The server sends every player and item in the potentially visible set - behind
the camera, round the corner, the far side of a big room - and each of them
used to be lerped on the CPU into the silhouette mesh and built into the
structure every frame, a cost that grows with the player count and was paid
for models that could not touch a single traced pixel.

What an entity can affect is bounded: its shadow reaches at most `reach`
units from it (r_rtActorShadowLength), its occlusion at most the AO radius.
So it matters only if its bounding sphere, grown by that reach, meets the view
frustum - tested against the four side planes, which also exclude everything
behind the eye beyond the reach. A player just behind the camera whose shadow
falls forward into view stays in; one across the map does not.

The level itself is not culled and does not need to be: it is built once at
map load, and rays only start from visible pixels, so unseen geometry costs
only when a visible ray reaches it - which is exactly when it has to be there.
=================
*/
static qboolean rt_in_reach( const trRefEntity_t *ent, float radius, float reach )
{
	int p;

	if ( !r_rtCull->integer ) {
		return qtrue;
	}
	for ( p = 0; p < 4; p++ ) {
		const cplane_t *pl = &backEnd.viewParms.frustum[p];
		if ( DotProduct( ent->e.origin, pl->normal ) - pl->dist < -( radius + reach ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/* how far an entity's effect on the traced passes can reach, per structure */
static float rt_actor_reach( void )
{
	float r = r_rtaoRadius->value;
	if ( rtf.actorMesh ) {
		/* as far as a shadow ray can meet a player - the same bound the shadow
		   pass's per-pixel test uses (vk_rt_shadow.c, E204) */
		const float reach = r_rtActorShadowLength->value;
		const float disc = r_rtActorShadowSoftness->value * ( reach / 128.0f > 1.0f ? reach / 128.0f : 1.0f );
		const float bound = 1.25f * ( reach + disc + 8.0f ) + 32.0f;
		if ( bound > r ) {
			r = bound;
		}
	}
	return r + 16.0f;
}

static uint32_t rt_build_actor_mesh( int idx )
{
	float *vout = (float *)vk.rt.world.actor_vertex_ptr[idx];
	uint32_t *iout = (uint32_t *)vk.rt.world.actor_index_ptr[idx];
	uint32_t nv = 0, nt = 0, nents = 0;
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	VkAccelerationStructureBuildRangeInfoKHR range;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges[1];
	VkMemoryBarrier barrier;
	int e;

	const float reach = rt_actor_reach();
	uint32_t culled = 0;
	uint32_t sig = 2166136261u;   /* [QL] E205: which models, in order - see the build below */
	qboolean refit;

	vk.rt.world.actorTris = 0;
	vk.rt.world.actorEntities = 0;
	vk.rt.world.actorCulled = 0;
	vk.rt.world.actorDropped = 0;
	vk.rt.world.actorSphereCount = 0;
	if ( !vk.rt.world.actorReady || vout == NULL || iout == NULL ) {
		return 0;
	}

	/*
	[QL] E214: casters, not model parts.

	A player is four or five MD3s (legs, torso, head, weapon) and the shadow
	pass used to see each as its own sphere, regroup them per pixel by
	distance, and look the light up at whatever centre that gave - so two
	pixels of one shadow could use two lights, and a pickup beside a player
	could join them. Here the parts are grouped once, by the lighting origin
	cgame gives every part of one player (RF_LIGHTING_ORIGIN, so they are lit
	alike) or the entity's own origin, into one caster: a sphere around every
	part over both animation frames, the light grid's direction at it -
	R_LightForPoint, the sample Quake 3 lights the model itself with - and the
	range of its triangles, which are emitted caster by caster so the range is
	contiguous and the shadow pass can tell its hits from another's.
	*/
	{
		static int		partEnt[MAX_REFENTITIES];
		static int		partCaster[MAX_REFENTITIES];
		static float	partSphere[MAX_REFENTITIES][4];
		float			castKey[RT_MAX_SHADOW_ACTORS][3];
		float			castSum[RT_MAX_SHADOW_ACTORS][3];
		int				castParts[RT_MAX_SHADOW_ACTORS];
		int				nparts = 0, ncast = 0, p, c, k;

		vk.rt.world.actorListComplete = qtrue;
		for ( e = 0; e < backEnd.refdef.num_entities && nparts < MAX_REFENTITIES; e++ ) {
			const trRefEntity_t *ent = &backEnd.refdef.entities[e];
			const model_t *mod;
			const md3Header_t *header;
			const float *key;
			float pc[2][3], pr[2], scale, d;
			int fi;

			if ( ent->e.reType != RT_MODEL ) {
				continue;
			}
			/* the view weapon, things made of light, things that cast nothing,
			   and shells/effects drawn over another entity with a custom shader */
			if ( ent->e.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK | RF_NOOCCLUDE | RF_NOSHADOW ) ) {
				continue;
			}
			if ( ent->e.customShader ) {
				continue;
			}
			mod = R_GetModelByHandle( ent->e.hModel );
			if ( mod == NULL || mod->type != MOD_MESH || mod->md3[0] == NULL ) {
				continue;
			}
			header = mod->md3[0];
			/* [QL] E203/E214: the part's bound over BOTH frames it is lerped
			   between, scaled by the largest axis - the current frame alone
			   could leave the lerped mesh outside its own sphere */
			scale = VectorLength( ent->e.axis[0] );
			d = VectorLength( ent->e.axis[1] ); if ( d > scale ) scale = d;
			d = VectorLength( ent->e.axis[2] ); if ( d > scale ) scale = d;
			if ( scale <= 0.0f ) scale = 1.0f;
			for ( fi = 0; fi < 2; fi++ ) {
				int f = fi ? ent->e.oldframe : ent->e.frame;
				const md3Frame_t *fr;
				if ( f < 0 || f >= header->numFrames ) f = 0;
				fr = (const md3Frame_t *)( (const byte *)header + header->ofsFrames ) + f;
				for ( k = 0; k < 3; k++ ) {
					pc[fi][k] = ent->e.origin[k] + ent->e.axis[0][k] * fr->localOrigin[0] +
						ent->e.axis[1][k] * fr->localOrigin[1] + ent->e.axis[2][k] * fr->localOrigin[2];
				}
				pr[fi] = fr->radius * scale;
			}
			for ( k = 0; k < 3; k++ ) {
				partSphere[nparts][k] = 0.5f * ( pc[0][k] + pc[1][k] );
			}
			partSphere[nparts][3] = 0.5f * Distance( pc[0], pc[1] ) + ( pr[0] > pr[1] ? pr[0] : pr[1] );
			if ( !rt_in_reach( ent, partSphere[nparts][3] + Distance( partSphere[nparts], ent->e.origin ), reach ) ) {
				culled++;
				continue;
			}
			key = ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) ? ent->e.lightingOrigin : ent->e.origin;
			for ( c = 0; c < ncast; c++ ) {
				if ( fabsf( castKey[c][0] - key[0] ) < 0.01f && fabsf( castKey[c][1] - key[1] ) < 0.01f &&
					 fabsf( castKey[c][2] - key[2] ) < 0.01f ) {
					break;
				}
			}
			if ( c == ncast ) {
				if ( ncast < RT_MAX_SHADOW_ACTORS ) {
					VectorCopy( key, castKey[ncast] );
					VectorClear( castSum[ncast] );
					castParts[ncast] = 0;
					ncast++;
				} else {
					c = -1;   /* not listed: emitted last, traced the old way */
					vk.rt.world.actorListComplete = qfalse;
				}
			}
			if ( c >= 0 ) {
				VectorAdd( castSum[c], partSphere[nparts], castSum[c] );
				castParts[c]++;
			}
			partEnt[nparts] = e;
			partCaster[nparts] = c;
			nparts++;
		}

		/* each caster's sphere and light */
		for ( c = 0; c < ncast; c++ ) {
			float *sp = vk.rt.world.actorSphere[c];
			float *lt = vk.rt.world.actorLight[c];
			vec3_t amb, dl, dir, key;
			VectorScale( castSum[c], 1.0f / (float)castParts[c], sp );
			sp[3] = 0.0f;
			for ( p = 0; p < nparts; p++ ) {
				if ( partCaster[p] == c ) {
					const float r = Distance( sp, partSphere[p] ) + partSphere[p][3];
					if ( r > sp[3] ) sp[3] = r;
				}
			}
			VectorCopy( castKey[c], key );
			lt[3] = 0.0f;
			VectorSet( lt, 0.0f, 0.0f, 1.0f );
			if ( R_LightForPoint( key, amb, dl, dir ) && VectorNormalize( dir ) > 0.0f ) {
				/* never flatter than 30 degrees above the ground - Quake 3's
				   RB_ProjectionShadowDeform: a shadow cast off a light at the
				   horizon runs across the whole floor */
				if ( dir[2] < 0.5f ) {
					dir[2] = 0.5f;
					VectorNormalize( dir );
				}
				VectorCopy( dir, lt );
				lt[3] = ( dl[0] + dl[1] + dl[2] > 0.0f ) ? 1.0f : 0.0f;
			}
		}
		/* counted up as each caster's triangles are complete, so a mesh that
		   fills up part-way lists only the casters it holds whole */
		vk.rt.world.actorSphereCount = 0;
		vk.rt.world.actorRestStart = 0;

		/* emit: caster by caster, then the unlisted ones */
		for ( c = 0; c <= ncast; c++ ) {
			const int want = c < ncast ? c : -1;
			const uint32_t firstTri = nt;
			/*
			[QL] E219: whole casters only. What a caster needs - every part's
			surfaces and its caps - is counted before any of it is written; a
			caster that does not fit is left out entirely and counted, rather
			than written up to the point the mesh ran out (a player cut off at
			the waist) or without its caps (a hollow shadow). The unlisted
			casters past RT_MAX_SHADOW_ACTORS are checked part by part.
			*/
			if ( want >= 0 ) {
				uint32_t needV = 0, needT = 0;
				for ( p = 0; p < nparts; p++ ) {
					const md3Header_t *h;
					const md3Surface_t *sf;
					const rtCapInfo_t *cp;
					int si;
					if ( partCaster[p] != want ) {
						continue;
					}
					h = R_GetModelByHandle( backEnd.refdef.entities[ partEnt[p] ].e.hModel )->md3[0];
					sf = (const md3Surface_t *)( (const byte *)h + h->ofsSurfaces );
					for ( si = 0; si < h->numSurfaces; si++ ) {
						needV += (uint32_t)sf->numVerts;
						needT += (uint32_t)sf->numTriangles;
						sf = (const md3Surface_t *)( (const byte *)sf + sf->ofsEnd );
					}
					cp = r_rtActorCaps->integer ? rt_caps_for( h ) : NULL;
					if ( cp && cp->numLoops > 0 ) {
						needV += (uint32_t)cp->numLoops;
						needT += (uint32_t)cp->numEdges;
					}
				}
				if ( nv + needV > RT_ACTOR_MAX_VERTS || nt + needT > RT_ACTOR_MAX_TRIS ) {
					vk.rt.world.actorDropped++;
					vk.rt.world.actorListComplete = qfalse;
					/* its sphere stays listed for the early out (conservative);
					   an empty range means nothing of it is hit */
					vk.rt.world.actorRange[c][0] = vk.rt.world.actorRange[c][1] = (float)nt;
					vk.rt.world.actorRange[c][2] = vk.rt.world.actorRange[c][3] = 0.0f;
					vk.rt.world.actorSphereCount = c + 1;
					vk.rt.world.actorRestStart = nt;
					continue;
				}
			}
			for ( p = 0; p < nparts; p++ ) {
				const trRefEntity_t *ent;
				const model_t *mod;
				const md3Header_t *header;
				const md3Surface_t *surf;
				int s, frame, oldframe;
				float backlerp;
				uint32_t entBase;

				if ( partCaster[p] != want ) {
					continue;
				}
				ent = &backEnd.refdef.entities[ partEnt[p] ];
				mod = R_GetModelByHandle( ent->e.hModel );
				header = mod->md3[0];
				if ( want < 0 ) {
					/* an unlisted part: whole or not at all, the same way */
					uint32_t needV = 0, needT = 0;
					const md3Surface_t *sf = (const md3Surface_t *)( (const byte *)header + header->ofsSurfaces );
					const rtCapInfo_t *cp = r_rtActorCaps->integer ? rt_caps_for( header ) : NULL;
					for ( s = 0; s < header->numSurfaces; s++ ) {
						needV += (uint32_t)sf->numVerts;
						needT += (uint32_t)sf->numTriangles;
						sf = (const md3Surface_t *)( (const byte *)sf + sf->ofsEnd );
					}
					if ( cp && cp->numLoops > 0 ) {
						needV += (uint32_t)cp->numLoops;
						needT += (uint32_t)cp->numEdges;
					}
					if ( nv + needV > RT_ACTOR_MAX_VERTS || nt + needT > RT_ACTOR_MAX_TRIS ) {
						vk.rt.world.actorDropped++;
						continue;
					}
				}
			frame = ent->e.frame;
			oldframe = ent->e.oldframe;
			if ( frame < 0 || frame >= header->numFrames ) frame = 0;
			if ( oldframe < 0 || oldframe >= header->numFrames ) oldframe = 0;
			backlerp = ent->e.backlerp;
			entBase = nv;

			surf = (const md3Surface_t *)( (const byte *)header + header->ofsSurfaces );
			for ( s = 0; s < header->numSurfaces; s++ ) {
				const short *newXyz, *oldXyz;
				const int *tris;
				int v, t;

				if ( nv + (uint32_t)surf->numVerts > RT_ACTOR_MAX_VERTS ||
					 nt + (uint32_t)surf->numTriangles > RT_ACTOR_MAX_TRIS ) {
					goto full;
				}
				newXyz = (const short *)( (const byte *)surf + surf->ofsXyzNormals ) + frame * surf->numVerts * 4;
				oldXyz = (const short *)( (const byte *)surf + surf->ofsXyzNormals ) + oldframe * surf->numVerts * 4;
				for ( v = 0; v < surf->numVerts; v++, newXyz += 4, oldXyz += 4 ) {
					vec3_t l;
					float *o = &vout[ ( nv + v ) * 3 ];
					l[0] = ( newXyz[0] * ( 1.0f - backlerp ) + oldXyz[0] * backlerp ) * MD3_XYZ_SCALE;
					l[1] = ( newXyz[1] * ( 1.0f - backlerp ) + oldXyz[1] * backlerp ) * MD3_XYZ_SCALE;
					l[2] = ( newXyz[2] * ( 1.0f - backlerp ) + oldXyz[2] * backlerp ) * MD3_XYZ_SCALE;
					o[0] = ent->e.origin[0] + ent->e.axis[0][0] * l[0] + ent->e.axis[1][0] * l[1] + ent->e.axis[2][0] * l[2];
					o[1] = ent->e.origin[1] + ent->e.axis[0][1] * l[0] + ent->e.axis[1][1] * l[1] + ent->e.axis[2][1] * l[2];
					o[2] = ent->e.origin[2] + ent->e.axis[0][2] * l[0] + ent->e.axis[1][2] * l[1] + ent->e.axis[2][2] * l[2];
				}
				tris = (const int *)( (const byte *)surf + surf->ofsTriangles );
				for ( t = 0; t < surf->numTriangles * 3; t++ ) {
					iout[ nt * 3 + t ] = nv + (uint32_t)tris[t];
				}
				nv += surf->numVerts;
				nt += surf->numTriangles;
				surf = (const md3Surface_t *)( (const byte *)surf + surf->ofsEnd );
			}
			{
				/* [QL] E180/E184: close the model's holes - see rt_caps_for */
				const rtCapInfo_t *cap = r_rtActorCaps->integer ? rt_caps_for( header ) : NULL;
				if ( cap && cap->numLoops > 0 &&
					 nv + (uint32_t)cap->numLoops <= RT_ACTOR_MAX_VERTS &&
					 nt + (uint32_t)cap->numEdges <= RT_ACTOR_MAX_TRIS ) {
					float cnt[RT_CAP_LOOPS];
					int l, k;
					for ( l = 0; l < cap->numLoops; l++ ) {
						VectorClear( &vout[ ( nv + l ) * 3 ] );
						cnt[l] = 0.0f;
					}
					for ( k = 0; k < cap->numEdges; k++ ) {
						const int *ed = &cap->edges[k * 3];
						VectorAdd( &vout[ ( nv + ed[2] ) * 3 ], &vout[ ( entBase + ed[0] ) * 3 ], &vout[ ( nv + ed[2] ) * 3 ] );
						cnt[ ed[2] ] += 1.0f;
					}
					for ( l = 0; l < cap->numLoops; l++ ) {
						VectorScale( &vout[ ( nv + l ) * 3 ], 1.0f / ( cnt[l] > 0.0f ? cnt[l] : 1.0f ), &vout[ ( nv + l ) * 3 ] );
					}
					for ( k = 0; k < cap->numEdges; k++ ) {
						const int *ed = &cap->edges[k * 3];
						iout[ nt * 3 + 0 ] = nv + (uint32_t)ed[2];
						iout[ nt * 3 + 1 ] = entBase + (uint32_t)ed[1];
						iout[ nt * 3 + 2 ] = entBase + (uint32_t)ed[0];
						nt++;
					}
					nv += (uint32_t)cap->numLoops;
				}
			}
			nents++;
			sig = sig * 1000003u + (uint32_t)ent->e.hModel;   /* [QL] E205 */
			}
			if ( c < ncast ) {
				vk.rt.world.actorRange[c][0] = (float)firstTri;
				vk.rt.world.actorRange[c][1] = (float)nt;
				vk.rt.world.actorRange[c][2] = vk.rt.world.actorRange[c][3] = 0.0f;
				vk.rt.world.actorSphereCount = c + 1;
				vk.rt.world.actorRestStart = nt;
			} else {
				vk.rt.world.actorRestStart = firstTri;
			}
		}
	}
	if ( 0 ) {
full:
		/* [QL] E214: out of room part-way - what was emitted after the last
		   whole caster is unlisted */
		vk.rt.world.actorListComplete = qfalse;
	}
	vk.rt.world.actorCulled = culled;   /* [QL] E203 */
	if ( nt == 0 ) {
		vk.rt.world.actorBuiltValid[idx] = qfalse;
		return 0;
	}

	/*
	[QL] E205: refit rather than rebuild, when nothing but positions changed.

	The same models in the same order give the same triangles in the same
	index order - only where the vertices are has moved. A structure built with
	ALLOW_UPDATE can then be refitted in place: the tree is kept and its boxes
	are recomputed, which is several times cheaper than building one. Each
	command buffer's structure is compared with what that buffer itself last
	built (the buffers alternate). The tree was shaped for the pose it was
	built in, so it gets looser as players animate away from it: a full build
	every 30 refits keeps tracing fast.
	*/
	sig = sig * 1000003u + nv;
	sig = sig * 1000003u + nt;
	refit = r_rtActorRefit->integer && vk.rt.world.actorBuiltValid[idx] && vk.rt.world.actorBuiltSig[idx] == sig &&
		vk.rt.world.actorRefits[idx] < 30;
	if ( refit ) {
		vk.rt.world.actorRefits[idx]++;
	} else {
		vk.rt.world.actorBuiltSig[idx] = sig;
		vk.rt.world.actorBuiltValid[idx] = qtrue;
		vk.rt.world.actorRefits[idx] = 0;
	}
	vk.rt.world.actorRefitFrames += refit ? 1 : 0;
	vk.rt.world.actorBuildFrames += refit ? 0 : 1;

	rt_actor_geometry( &geom, idx, nv );
	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
		VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;   /* [QL] E205 */
	build_info.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.srcAccelerationStructure = refit ? vk.rt.world.actor_blas[idx] : VK_NULL_HANDLE;
	build_info.dstAccelerationStructure = vk.rt.world.actor_blas[idx];
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;
	build_info.scratchData.deviceAddress = vk.rt.world.actor_scratch_address[idx];

	Com_Memset( &range, 0, sizeof( range ) );
	range.primitiveCount = nt;
	ranges[0] = &range;
	qvkCmdBuildAccelerationStructuresKHR( vk.cmd->command_buffer, 1, &build_info, ranges );

	/* the top level built next reads it */
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, NULL, 0, NULL );

	vk.rt.world.actorTris = nt;
	vk.rt.world.actorEntities = nents;
	return nt;
}


/*
=================
vk_rt_create_dynamic

The two proxy shapes every dynamic entity is instanced from, and the per-frame
structures that hold those instances.

Everything here is sized and allocated once. The per-frame work is then writing
an instance array and one vkCmdBuildAccelerationStructures into a structure that
already exists at the right size - no allocation, no queue wait, nothing that
can fail in the middle of a frame.

Any failure leaves dynReady false, the descriptors pointing at the static world
structure, and the occlusion pass exactly as it was.
=================
*/
static qboolean vk_rt_create_dynamic( void )
{
	/* A unit cube centred on the origin. The instance transform scales it to
	   the entity's bounds, so the geometry is the same for every entity and is
	   built once. */
	static const float boxVerts[8][3] = {
		{ -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
		{ -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
		{ -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
		{ -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f },
	};
	/*
	Wound so every triangle's normal points out of the box. That is not
	cosmetic: the occlusion trace culls back faces on these instances, and which
	face is the back one is decided by this winding. Reverse a triple here and
	that box stops occluding from the outside and starts occluding from the
	inside, which is the bug this arrangement exists to prevent -
	rt_proxy_winding_is_outward now checks it rather than trusting this comment.
	*/
	static const uint32_t boxIndices[36] = {
		0,3,1, 0,2,3,   4,7,6, 4,5,7,   /* -z, +z */
		0,5,4, 0,1,5,   2,7,3, 2,6,7,   /* -y, +y */
		0,6,2, 0,4,6,   1,7,5, 1,3,7,   /* -x, +x */
	};
	static float ballVerts[RT_BALL_VERTS][3];
	static uint32_t ballIndices[RT_BALL_TRIS * 3];
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	VkAccelerationStructureBuildSizesInfoKHR sizes;
	VkAccelerationStructureCreateInfoKHR create_info;
	VkDeviceSize scratchAlign;
	uint32_t maxInstances = RT_MAX_DYN_INSTANCES;
	uint32_t i;
	VkResult res;

	vk_rt_destroy_dynamic();

	if ( vk.rt.world.blas == VK_NULL_HANDLE ) {
		return qfalse;
	}

	// ---- the two proxy shapes ----
	if ( !rt_create_proxy( &vk.rt.world.proxy_box, boxVerts, 8,
			boxIndices, 12, "entity proxy box BLAS" ) ) {
		goto fail;
	}

	rt_build_ball_mesh( ballVerts, ballIndices );
	if ( !rt_create_proxy( &vk.rt.world.proxy_ball, ballVerts, RT_BALL_VERTS,
			ballIndices, RT_BALL_TRIS, "entity proxy ball BLAS" ) ) {
		goto fail;
	}

	// ---- per-frame top level, sized for the worst case and built into forever ----
	Com_Memset( &geom, 0, sizeof( geom ) );
	geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geom.geometry.instances.arrayOfPointers = VK_FALSE;

	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	/* FAST_BUILD, not FAST_TRACE: this one is rebuilt every frame, and a
	   structure of a couple of hundred boxes is traced against far fewer times
	   than the world one it sits beside. */
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;

	Com_Memset( &sizes, 0, sizeof( sizes ) );
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	qvkGetAccelerationStructureBuildSizesKHR( vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &maxInstances, &sizes );

	if ( sizes.accelerationStructureSize == 0 ) {
		goto fail;
	}

	scratchAlign = rt_scratch_alignment();

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( !rt_create_buffer( sizes.accelerationStructureSize,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
				&vk.rt.world.dyn_tlas_buffer[i], &vk.rt.world.dyn_tlas_memory[i] ) ) {
			goto fail;
		}

		Com_Memset( &create_info, 0, sizeof( create_info ) );
		create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		create_info.buffer = vk.rt.world.dyn_tlas_buffer[i];
		create_info.size = sizes.accelerationStructureSize;
		create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

		res = qvkCreateAccelerationStructureKHR( vk.device, &create_info, NULL, &vk.rt.world.dyn_tlas[i] );
		if ( res < 0 ) {
			vk.rt.world.dyn_tlas[i] = VK_NULL_HANDLE;
			goto fail;
		}

		if ( !rt_create_buffer( sizes.buildScratchSize + scratchAlign,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				&vk.rt.world.dyn_scratch_buffer[i], &vk.rt.world.dyn_scratch_memory[i] ) ) {
			goto fail;
		}
		vk.rt.world.dyn_scratch_address[i] =
			( rt_buffer_address( vk.rt.world.dyn_scratch_buffer[i] ) + scratchAlign - 1 ) & ~( scratchAlign - 1 );

		if ( !rt_create_host_buffer( sizeof( VkAccelerationStructureInstanceKHR ) * maxInstances,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
				&vk.rt.world.dyn_instance_buffer[i], &vk.rt.world.dyn_instance_memory[i],
				&vk.rt.world.dyn_instance_ptr[i] ) ) {
			goto fail;
		}
	}

	vk.rt.world.dyn_maxInstances = maxInstances;
	vk.rt.world.dynReady = qtrue;
	rt_create_actor_mesh();   /* [QL] E166: optional - failing loses only the silhouettes */
	return qtrue;

fail:
	ri.Printf( PRINT_WARNING, "RT: could not create the dynamic structures - "
		"entities will cast no occlusion (the map still will)\n" );
	vk_rt_destroy_dynamic();
	return qfalse;
}


/*
=================
vk_rt_build_dynamic_tlas

One instance of the world, plus a proxy per visible entity, built into this
command buffer's top level structure.

Recorded into the frame's own command buffer, between the main render pass
ending and the occlusion pass beginning - an acceleration structure build cannot
be inside a render pass, and that gap is the only place in the frame that is
outside one and still before the trace.
=================
*/
/*
[QL] One line per entity the structure considered, on request.

The instance count answered "are the proxies there at all". It cannot answer
the question that keeps coming back, which is *which* entity is the one making
a hard black box on a bridge railing - and that question has now been guessed
at from screenshots several times, wrongly, because a box proxy looks the same
whatever model it belongs to.

Names and numbers instead. Model name says what it is, the flags say why it was
kept or dropped, and the size against the origin says whether the box is
anywhere near the size of the thing it stands for - a long thin railing and a
compact post produce very different boxes and only one of them is a problem.
*/
static qboolean rtDumpRequested = qfalse;

void vk_rt_request_dump( void ) {
	rtDumpRequested = qtrue;
}

static void rt_dump_entity( int index, const trRefEntity_t *ent, const char *what ) {
	const model_t *mod = R_GetModelByHandle( ent->e.hModel );

	ri.Printf( PRINT_ALL, "  [%3i] %-28s reType %i rfx 0x%04x  %s\n",
		index,
		( mod != NULL && mod->name[0] ) ? mod->name : "<no model>",
		ent->e.reType, ent->e.renderfx, what );
}


static qboolean vk_rt_build_dynamic_tlas_inner( void );
/* [QL] E177: timed */
qboolean vk_rt_build_dynamic_tlas( void )
{
	qboolean r;
	vk_timing_begin( RTT_TLAS );
	r = vk_rt_build_dynamic_tlas_inner();
	vk_timing_end( RTT_TLAS );
	return r;
}

static qboolean vk_rt_build_dynamic_tlas_inner( void )
{
	VkAccelerationStructureInstanceKHR *inst;
	VkAccelerationStructureDeviceAddressInfoKHR addr_info;
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	VkAccelerationStructureBuildRangeInfoKHR range;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges[1];
	VkMemoryBarrier barrier;
	uint64_t worldRef, boxRef, ballRef;
	uint32_t count = 0;
	uint32_t numRound = 0;
	const int idx = vk.cmd_index;
	const qboolean dump = rtDumpRequested;
	int i, j;

	/* [QL] E203: see rt_in_reach; 0 means do not cull the proxies */
	const float proxyReach = ( r_ssr->integer && r_ssrRayTrace->integer ) ? 0.0f : r_rtaoRadius->value + 16.0f;

	rtDumpRequested = qfalse;

	if ( !vk.rt.world.dynReady ) {
		return qfalse;
	}
	backEnd.doneRTDynamic = qtrue;   /* [QL] E156: so the reflection pass need not build it again */

	inst = (VkAccelerationStructureInstanceKHR *)vk.rt.world.dyn_instance_ptr[idx];
	if ( inst == NULL ) {
		return qfalse;
	}

	Com_Memset( &addr_info, 0, sizeof( addr_info ) );
	addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addr_info.accelerationStructure = vk.rt.world.blas;
	worldRef = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr_info );

	addr_info.accelerationStructure = vk.rt.world.proxy_box.blas;
	boxRef = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr_info );

	addr_info.accelerationStructure = vk.rt.world.proxy_ball.blas;
	ballRef = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr_info );

	// the map, at identity
	Com_Memset( &inst[0], 0, sizeof( inst[0] ) );
	inst[0].transform.matrix[0][0] = 1.0f;
	inst[0].transform.matrix[1][1] = 1.0f;
	inst[0].transform.matrix[2][2] = 1.0f;
	/* [QL] E165: instance masks - 0x01 the level (the map and its movers),
	   0x02 players and items. Occlusion and the reflections trace 0xFF and
	   see both, as before; the shadow rays pick (r_rtShadowCasters). */
	inst[0].mask = RT_MASK_LEVEL;
	inst[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	inst[0].accelerationStructureReference = worldRef;
	count = 1;

	/* [QL] E166: the players' and items' real triangles, already in the world */
	if ( rt_actors_wanted() && rt_build_actor_mesh( idx ) > 0 ) {
		Com_Memset( &inst[count], 0, sizeof( inst[count] ) );
		inst[count].transform.matrix[0][0] = 1.0f;
		inst[count].transform.matrix[1][1] = 1.0f;
		inst[count].transform.matrix[2][2] = 1.0f;
		inst[count].mask = RT_MASK_SILHOUETTE;
		/* both sides: a model is not always closed, and a shadow ray must stop
		   at whichever face of an arm it meets */
		inst[count].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		addr_info.accelerationStructure = vk.rt.world.actor_blas[idx];
		inst[count].accelerationStructureReference = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr_info );
		count++;
	}

	for ( i = 0; r_rtDynamic->integer && i < backEnd.refdef.num_entities &&
			count < vk.rt.world.dyn_maxInstances; i++ ) {
		const trRefEntity_t *ent = &backEnd.refdef.entities[i];
		vec3_t mins, maxs, size, centre;

		/*
		The view weapon and the player's own body. One is drawn a few units
		from the eye with a squashed depth range and the other is not drawn at
		all in first person - either would put a large occluder around the
		camera and darken the whole view from inside it.
		*/
		if ( ent->e.renderfx & ( RF_FIRST_PERSON | RF_THIRD_PERSON ) ) {
			if ( dump ) rt_dump_entity( i, ent, "skipped - view model" );
			continue;
		}

		/*
		Things made of light. cgame marks these because no proxy shape is right
		for them - see RF_NOOCCLUDE. The explosion flash is the one that made
		the flag necessary: a flat dish model, randomly rotated about the impact
		normal, whose box landed on the wall as a hard-edged square at a random
		angle on every single impact.
		*/
		if ( ent->e.renderfx & RF_NOOCCLUDE ) {
			if ( dump ) rt_dump_entity( i, ent, "skipped - RF_NOOCCLUDE" );
			continue;
		}

		if ( !R_GetEntityModelBounds( ent, mins, maxs ) ) {
			if ( dump ) rt_dump_entity( i, ent, "skipped - no bounds" );
			continue;
		}

		VectorSubtract( maxs, mins, size );
		if ( size[0] <= 0.0f || size[1] <= 0.0f || size[2] <= 0.0f ) {
			if ( dump ) rt_dump_entity( i, ent, "skipped - empty bounds" );
			continue;
		}
		VectorAdd( mins, maxs, centre );
		VectorScale( centre, 0.5f, centre );

		/* [QL] E203: a proxy reaches only as far as the occlusion radius - but
		   the ray-traced reflection fallback can show it from anywhere, so no
		   culling while that is on */
		if ( proxyReach > 0.0f && !rt_in_reach( ent, 0.5f * VectorLength( size ) + VectorLength( centre ), proxyReach ) ) {
			if ( dump ) rt_dump_entity( i, ent, "skipped - out of reach (r_rtCull)" );
			continue;
		}

		/*
		A 3x4 row-major transform taking the unit box to this entity's oriented
		box: each column is one of the entity's axes scaled to that side of the
		model's bounds, and the last column is where the box's centre lands.

		Oriented and not axis-aligned. The alternative - rotate the model box's
		corners, take the world box of those, and use a diagonal matrix - is a
		box up to 41% wider on each horizontal axis than the thing inside it,
		which for an upright player turning on the spot is a lot of occlusion
		coming out of empty air. The instance transform is a full 3x4 and can
		hold the rotation, so there is no reason to throw it away.

		The axes are used as they come. Quake lets an entity carry
		non-normalized axes to scale a model, and multiplying by them rather
		than by a normalized copy is what keeps the box on such a model the
		right size.
		*/
		Com_Memset( &inst[count], 0, sizeof( inst[count] ) );
		for ( j = 0; j < 3; j++ ) {
			inst[count].transform.matrix[j][0] = ent->e.axis[0][j] * size[0];
			inst[count].transform.matrix[j][1] = ent->e.axis[1][j] * size[1];
			inst[count].transform.matrix[j][2] = ent->e.axis[2][j] * size[2];
			inst[count].transform.matrix[j][3] = ent->e.origin[j]
				+ ent->e.axis[0][j] * centre[0]
				+ ent->e.axis[1][j] * centre[1]
				+ ent->e.axis[2][j] * centre[2];
		}
		{
			/* a door, a lift, a platform - brush models are the level moving */
			const model_t *mod = R_GetModelByHandle( ent->e.hModel );
			inst[count].mask = ( ent->e.reType == RT_MODEL && mod && mod->type == MOD_BRUSH )
				? RT_MASK_LEVEL : RT_MASK_ACTORS;
		}
		/*
		[QL] Back faces cull on these, and that is what stops an entity's box
		from occluding the entity.

		The visible surface of a model is inside its own bounding box, so a ray
		leaving that surface starts inside the box and hits the far wall a few
		units later - every entity came out fully occluded, which in the debug
		view is a black model and in the scene is a black model. The box is the
		occluder, and it was occluding the thing it stands for.

		A ray that starts inside a closed box can only hit it from the inside,
		and with the winding above that is the back face. So culling back faces
		lets those rays leave, while rays arriving from outside - the floor
		under the model, the wall behind it - still hit the front face and are
		occluded, which is the contact darkening this is all for.

		No flip flag, and that is the whole of the previous fix's mistake. I
		added FRONT_COUNTERCLOCKWISE believing Vulkan's default was that
		clockwise-from-the-ray is the front face; it is counter-clockwise, so
		the flag inverted a test that was already right and the boxes occluded
		from the inside and nowhere else. That is not deduced from the
		specification, it is read off a controlled pair of screenshots at the
		same radius: with the flag, entities were black and the floor beneath
		them was untouched - inside hits kept, outside hits culled, exactly
		inverted. Leaving flags at zero takes the default, under which a box
		wound outwards is front-facing from outside and back-facing from
		within.

		The world instance keeps CULL_DISABLE and is unaffected - a map is not a
		closed shell and its surfaces have to stop rays from either side.
		*/
		inst[count].flags = 0;

		/*
		Box or ball, decided by cgame. Same transform either way - the ball is
		the unit sphere, so the matrix that takes the unit box to the entity's
		bounds takes the sphere to the ellipsoid inscribed in them.

		The box is right for the things that are box-shaped or that rest on the
		floor: a door is a box, and a player standing on ground wants the full
		footprint, which an ellipsoid touching the floor at one point does not
		give. The ball is right for a projectile - see RF_OCCLUDE_ROUND.
		*/
		if ( ent->e.renderfx & RF_OCCLUDE_ROUND ) {
			inst[count].accelerationStructureReference = ballRef;
			numRound++;
		} else {
			inst[count].accelerationStructureReference = boxRef;
		}

		if ( dump ) {
			rt_dump_entity( i, ent, va( "%s  size %.0f %.0f %.0f  at %.0f %.0f %.0f",
				( ent->e.renderfx & RF_OCCLUDE_ROUND ) ? "ball" : "BOX ",
				size[0], size[1], size[2],
				inst[count].transform.matrix[0][3],
				inst[count].transform.matrix[1][3],
				inst[count].transform.matrix[2][3] ) );
		}

		count++;
	}

	if ( dump ) {
		ri.Printf( PRINT_ALL, "RT: %i of %i scene entit%s in the structure, %i round\n",
			(int)count - 1, backEnd.refdef.num_entities,
			backEnd.refdef.num_entities == 1 ? "y" : "ies", (int)numRound );
	}

	Com_Memset( &geom, 0, sizeof( geom ) );
	geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geom.geometry.instances.arrayOfPointers = VK_FALSE;
	geom.geometry.instances.data.deviceAddress = rt_buffer_address( vk.rt.world.dyn_instance_buffer[idx] );

	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.dstAccelerationStructure = vk.rt.world.dyn_tlas[idx];
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;
	build_info.scratchData.deviceAddress = vk.rt.world.dyn_scratch_address[idx];

	Com_Memset( &range, 0, sizeof( range ) );
	range.primitiveCount = count;
	ranges[0] = &range;

	/*
	[QL] Say what actually went into the structure, once per map.

	Two theories about why entities come out wrong look identical from a
	screenshot - the proxies are in the structure and occluding badly, or they
	were never added and the darkness is something else entirely - and guessing
	between them has already cost a build. The count separates them: 1 means the
	map and nothing else, and every explanation involving the boxes is wrong.
	*/
	if ( !rtDynReported ) {
		rtDynReported = qtrue;
		ri.Printf( PRINT_ALL, "RT: dynamic structure holds %i instance(s) - the map%s\n",
			(int)count, count > 1 ? va( " and %i entit%s (%i round)", (int)count - 1,
				count == 2 ? "y" : "ies", (int)numRound ) : " alone" );
	}

	if ( !rtDynRoundReported && numRound > 0 ) {
		rtDynRoundReported = qtrue;
		ri.Printf( PRINT_ALL, "RT: first round proxy - %i of %i entity instance(s) "
			"are ellipsoids (projectiles, gibs, brass)\n",
			(int)numRound, (int)count - 1 );
	}

	qvkCmdBuildAccelerationStructuresKHR( vk.cmd->command_buffer, 1, &build_info, ranges );

	/* The occlusion pass traces against what was just written. Without this the
	   fragment shader may read a structure the build has not finished. */
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL );

	return qtrue;
}

/*
=================
vk_rt_prebuild_dynamic

[QL] E165. Build this frame's structure before the 3D is drawn, when the shadow
rays in the main pass will trace it.

It used to be built only by the occlusion pass, part-way through the 3D (and by
the reflection pass after it), because an acceleration structure cannot be
built inside a render pass and that was the first gap in one. The shadow rays
run earlier - the model-shadow pass during the opaque surfaces - so reading it
there meant a structure a frame or two old, which the later build in the same
command buffer then rewrote under the reads.

So when they need it, the main pass is paused here, before the view is cleared,
the structure is built, and the frame carries on in the after-composite pass -
the one occlusion and reflections already continue in, identical to main
except that it loads what is there. Occlusion and reflections find it built
(backEnd.doneRTDynamic) and do not build it again, so nothing rewrites it after
the main pass has read it.
=================
*/
void vk_rt_prebuild_dynamic( void )
{
	if ( !vk.rtActive || !vk.rt.world.dynReady || !vk.rt.world.mainTlasWritten || backEnd.doneRTDynamic ) {
		return;
	}
	if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP ) {
		return;
	}
	if ( !rtf.anyShadow ) {
		return;
	}

	vk_end_render_pass();
	vk_rt_build_dynamic_tlas();

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.after_composite,
		vk.framebuffers.main[ vk.cmd->swapchain_image_index ], qfalse, vk.renderWidth, vk.renderHeight );
}


void vk_rt_destroy_world( void )
{
	if ( !vk.rt.world.worldBuilt && vk.rt.world.blas == VK_NULL_HANDLE ) {
		return;
	}
	/* Handles are freed newest first and every one is guarded, because this is
	   also the cleanup path for a build that failed halfway. */
	if ( vk.rt.world.tlas != VK_NULL_HANDLE ) {
		qvkDestroyAccelerationStructureKHR( vk.device, vk.rt.world.tlas, NULL );
	}
	if ( vk.rt.world.blas != VK_NULL_HANDLE ) {
		qvkDestroyAccelerationStructureKHR( vk.device, vk.rt.world.blas, NULL );
	}
	if ( vk.rt.world.tlas_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.rt.world.tlas_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.tlas_memory, NULL );
	}
	if ( vk.rt.world.blas_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.rt.world.blas_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.blas_memory, NULL );
	}
	if ( vk.rt.world.instance_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.rt.world.instance_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.instance_memory, NULL );
	}
	if ( vk.rt.world.vertex_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.rt.world.vertex_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.vertex_memory, NULL );
	}
	if ( vk.rt.world.index_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.rt.world.index_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.index_memory, NULL );
	}
	vk_rt_destroy_dynamic();   // [QL] R13 step 4, before the clear below zeroes its handles
	/* [QL] E156: the reflection's sets name the structure and the grid - stop
	   binding them before either goes */
	vk.ssr.rtReady = qfalse;
	if ( vk.rt.world.grid_buffer != VK_NULL_HANDLE ) {
		if ( vk.rt.world.grid_ptr != NULL ) {   /* E194: mapped only in system memory */
			qvkUnmapMemory( vk.device, vk.rt.world.grid_memory );
		}
		qvkDestroyBuffer( vk.device, vk.rt.world.grid_buffer, NULL );
		qvkFreeMemory( vk.device, vk.rt.world.grid_memory, NULL );
	}
	/*
	[QL] The map's half of vk.rt, and only that half.

	This cleared the whole of vk.rt, which also zeroed the ambient occlusion
	pass - aoReady, the depth view and sampler, the descriptor pool, both
	pipeline layouts and all four pipelines - without destroying any of them.
	Since this runs at the top of every world build, the effect was that AO
	worked on the first map after a vid_restart (the early-out above fires when
	nothing has been built yet, so the memset never ran) and was dead on every
	map after it, with the handles leaked and nothing in the log but "the pass
	was not created". Changing map looked like it broke ray tracing; restarting
	the video looked like it fixed it.

	See vk.h for why the fields moved into a sub-struct rather than this
	becoming a list of assignments.
	*/
	Com_Memset( &vk.rt.world, 0, sizeof( vk.rt.world ) );
}


/*
=================
vk_rt_build_world

Called once when a map finishes loading. Silent and harmless when ray query is
off, which is every ordinary build.
=================
*/
void vk_rt_build_world( const world_t *world )
{
	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR build_info;
	VkAccelerationStructureInstanceKHR instance;
	VkAccelerationStructureDeviceAddressInfoKHR addr_info;
	rtVertex_t *verts;
	uint32_t *indices;
	uint32_t vertexCount, indexCount, i;
	uint32_t totalVertices, totalIndices;
	VkDeviceSize vertexBytes, indexBytes;
	const msurface_t *worldSurfaces;
	uint32_t worldSurfaceCount;

	R_ResolveRTFeatures();   // [QL] K8: the load-time decisions read it too

	vk_rt_destroy_world();

	if ( !vk.rtActive || world == NULL || world->numsurfaces <= 0 ) {
		return;
	}

	/*
	[QL] Submodel 0 only. This structure is the part of the map that cannot move.

	world->surfaces holds every surface in the BSP, and the brush models - every
	door, plat, mover and brush entity - keep theirs in that same array, each
	one reached through its own submodel's firstSurface. Walking the array end
	to end therefore baked every door into the static structure at the position
	it was compiled at, where it stayed for the life of the map.

	Those doors are also entities and get a proxy that does follow them. So a
	door cast occlusion from two places at once: a shadow welded to its closed
	position and a second one tracking the real one. Reported as the AO having
	an open state and a closed state rather than an animation, which is what two
	occluders and one door look like.

	Submodel 0 is the world itself, and its range is the geometry that is
	actually static. Everything past it belongs to something drawn as an entity,
	and is occluded as one.
	*/
	worldSurfaces = world->surfaces;
	worldSurfaceCount = (uint32_t)world->numsurfaces;

	if ( world->bmodels != NULL && world->bmodels[0].numSurfaces > 0 &&
		 world->bmodels[0].numSurfaces <= world->numsurfaces ) {
		worldSurfaces = world->bmodels[0].firstSurface;
		worldSurfaceCount = (uint32_t)world->bmodels[0].numSurfaces;
	}
	if ( rt_getPhysicalDeviceProperties2 == NULL ) {
		rt_getPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
			ri.VK_GetInstanceProcAddr( vk_instance, "vkGetPhysicalDeviceProperties2" );
	}

	// pass one: how big
	vertexCount = 0;
	indexCount = 0;
	for ( i = 0; i < worldSurfaceCount; i++ ) {
		const msurface_t *surf = &worldSurfaces[i];

		if ( !rt_surface_is_occluder( surf ) ) {
			vk.rt.world.numSurfacesSkipped++;
			continue;
		}
		vk.rt.world.numSurfacesUsed++;
		rt_walk_surface( surf, NULL, NULL, &vertexCount, &indexCount );
	}

	if ( vertexCount == 0 || indexCount < 3 ) {
		ri.Printf( PRINT_WARNING, "RT: no world geometry to trace against "
			"(%i surfaces, %i skipped) - not building\n",
			world->numsurfaces, (int)vk.rt.world.numSurfacesSkipped );
		return;
	}

	vertexBytes = (VkDeviceSize)vertexCount * sizeof( rtVertex_t );
	indexBytes = (VkDeviceSize)indexCount * sizeof( uint32_t );
	totalVertices = vertexCount;
	totalIndices = indexCount;

	vk.rt.world.numVertices = totalVertices;
	vk.rt.world.numTriangles = totalIndices / 3;

	ri.Printf( PRINT_ALL, "RT: world geometry %i triangles from %i of %i surfaces (%i KiB)\n",
		(int)vk.rt.world.numTriangles, (int)vk.rt.world.numSurfacesUsed, (int)worldSurfaceCount,
		(int)( ( vertexBytes + indexBytes ) / 1024 ) );

	if ( worldSurfaceCount != (uint32_t)world->numsurfaces ) {
		ri.Printf( PRINT_ALL, "RT: %i surface(s) belong to movers and are occluded as entities\n",
			world->numsurfaces - (int)worldSurfaceCount );
	}

	if ( !rt_create_buffer( vertexBytes,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			&vk.rt.world.vertex_buffer, &vk.rt.world.vertex_memory ) ||
		 !rt_create_buffer( indexBytes,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			&vk.rt.world.index_buffer, &vk.rt.world.index_memory ) ) {
		vk_rt_destroy_world();
		return;
	}

	/*
	[QL] Vertices and indices are staged one after the other, from the zone.

	Both of those are fixes for the same field report: "Hunk_AllocateTempMemory:
	failed on 4194312" on a large map - 4194300 bytes of vertices, padded, plus
	the hunk header, which is 349525 vertices at 12 bytes each.

	Two things were wrong. The hunk is where the level itself has just been
	loaded, and asking it for several more megabytes at the end of that load is
	asking at the worst possible moment; ri.Malloc is the zone, a different pool
	with 64 MB of its own that the map does not compete for. And holding both
	arrays at once made the peak their sum when it only ever needed to be the
	larger of the two - they are filled and uploaded independently, so there is
	no reason for the first to still exist while the second is built.

	The two fill passes re-walk the same surfaces. That is safe because
	rt_walk_surface advances vertexCount and indexCount identically whether or
	not it is writing anything - but "safe because" is not "checked", so the
	totals are compared against the counting pass below.
	*/
	verts = (rtVertex_t *)ri.Malloc( (int)vertexBytes );
	if ( verts == NULL ) {
		ri.Printf( PRINT_WARNING, "RT: out of memory for %i KiB of vertices\n", (int)( vertexBytes / 1024 ) );
		vk_rt_destroy_world();
		return;
	}

	vertexCount = 0;
	indexCount = 0;
	for ( i = 0; i < worldSurfaceCount; i++ ) {
		const msurface_t *surf = &worldSurfaces[i];

		if ( !rt_surface_is_occluder( surf ) ) {
			continue;
		}
		rt_walk_surface( surf, verts, NULL, &vertexCount, &indexCount );
	}

	if ( vertexCount != totalVertices || indexCount != totalIndices ) {
		ri.Printf( PRINT_WARNING, "RT: vertex pass disagreed with the count (%i/%i verts, %i/%i indices)\n",
			(int)vertexCount, (int)totalVertices, (int)indexCount, (int)totalIndices );
		ri.Free( verts );
		vk_rt_destroy_world();
		return;
	}

	rt_upload( vk.rt.world.vertex_buffer, verts, vertexBytes );
	ri.Free( verts );

	indices = (uint32_t *)ri.Malloc( (int)indexBytes );
	if ( indices == NULL ) {
		ri.Printf( PRINT_WARNING, "RT: out of memory for %i KiB of indices\n", (int)( indexBytes / 1024 ) );
		vk_rt_destroy_world();
		return;
	}

	vertexCount = 0;
	indexCount = 0;
	for ( i = 0; i < worldSurfaceCount; i++ ) {
		const msurface_t *surf = &worldSurfaces[i];

		if ( !rt_surface_is_occluder( surf ) ) {
			continue;
		}
		rt_walk_surface( surf, NULL, indices, &vertexCount, &indexCount );
	}

	if ( vertexCount != totalVertices || indexCount != totalIndices ) {
		ri.Printf( PRINT_WARNING, "RT: index pass disagreed with the count (%i/%i verts, %i/%i indices)\n",
			(int)vertexCount, (int)totalVertices, (int)indexCount, (int)totalIndices );
		ri.Free( indices );
		vk_rt_destroy_world();
		return;
	}

	rt_upload( vk.rt.world.index_buffer, indices, indexBytes );
	ri.Free( indices );

	// ---- bottom level: the triangles ----
	Com_Memset( &geom, 0, sizeof( geom ) );
	geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;  // every surface here passed the opaque test
	geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geom.geometry.triangles.vertexData.deviceAddress = rt_buffer_address( vk.rt.world.vertex_buffer );
	geom.geometry.triangles.vertexStride = sizeof( rtVertex_t );
	geom.geometry.triangles.maxVertex = vertexCount - 1;
	geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geom.geometry.triangles.indexData.deviceAddress = rt_buffer_address( vk.rt.world.index_buffer );
	geom.geometry.triangles.transformData.deviceAddress = 0;

	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	/* PREFER_FAST_TRACE, not FAST_BUILD: this is built once at map load and
	   then traced against every frame for the length of the match. */
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;

	if ( !rt_build_acceleration_structure( &build_info, vk.rt.world.numTriangles,
			VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			&vk.rt.world.blas, &vk.rt.world.blas_buffer, &vk.rt.world.blas_memory, "world BLAS" ) ) {
		vk_rt_destroy_world();
		return;
	}

	// ---- top level: one instance of it, at identity ----
	Com_Memset( &instance, 0, sizeof( instance ) );
	/* A 3x4 row-major identity. The world is already in world space, so there
	   is nothing to transform - but the field is not optional and a zeroed
	   matrix collapses every triangle to the origin. */
	instance.transform.matrix[0][0] = 1.0f;
	instance.transform.matrix[1][1] = 1.0f;
	instance.transform.matrix[2][2] = 1.0f;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

	Com_Memset( &addr_info, 0, sizeof( addr_info ) );
	addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addr_info.accelerationStructure = vk.rt.world.blas;
	instance.accelerationStructureReference =
		qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr_info );

	if ( !rt_create_buffer( sizeof( instance ),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			&vk.rt.world.instance_buffer, &vk.rt.world.instance_memory ) ) {
		vk_rt_destroy_world();
		return;
	}
	rt_upload( vk.rt.world.instance_buffer, &instance, sizeof( instance ) );

	Com_Memset( &geom, 0, sizeof( geom ) );
	geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geom.geometry.instances.arrayOfPointers = VK_FALSE;
	geom.geometry.instances.data.deviceAddress = rt_buffer_address( vk.rt.world.instance_buffer );

	Com_Memset( &build_info, 0, sizeof( build_info ) );
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_info.geometryCount = 1;
	build_info.pGeometries = &geom;

	if ( !rt_build_acceleration_structure( &build_info, 1,
			VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			&vk.rt.world.tlas, &vk.rt.world.tlas_buffer, &vk.rt.world.tlas_memory, "world TLAS" ) ) {
		vk_rt_destroy_world();
		return;
	}

	vk.rt.world.worldBuilt = qtrue;
	rtaoOnReported = qfalse;   // [QL] report the AO state once for this map
	rtaoOffReported = qfalse;
	rtDynReported = qfalse;
	rtDynRoundReported = qfalse;

	/*
	[QL] R13 step 4.

	Skipped entirely when r_rtDynamic is off at map load, which leaves dynReady
	false, the descriptors naming the static world structure, and every frame
	exactly as it was before any of this existed - a real off switch, not a
	branch inside the new path. Toggling the cvar during a map still works and
	controls whether entities go into the structure; turning it off and
	reloading removes the structure as well.

	Failure is not failure of the map's structure either: it warns, leaves
	dynReady false, and the occlusion pass carries on tracing the world alone.
	*/
	if ( r_rtDynamic->integer ) {
		vk_rt_create_dynamic();
	}

	/*
	[QL] E156: the light grid, for the reflection's ray-traced fallback to
	light what it hits. Copied as the BSP stores it, eight bytes a point, and
	read by the shader directly. A map without a grid still gets a buffer - a
	binding has to name something - and haveGrid says not to read it.
	*/
	{
		const int points = world->lightGridData
			? world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] : 0;
		/* [QL] E176: the light field (R_BuildLightField) rides after the grid
		   in the same buffer, LF_STRIDE floats a point - no new binding, and the
		   reflection shader, which reads only the grid, never sees it */
		const VkDeviceSize gridBytes = points > 0 ? (VkDeviceSize)points * 8 : 16;
		const VkDeviceSize fieldBytes = ( points > 0 && world->lightField ) ? ( (VkDeviceSize)points * LF_STRIDE + 4 ) * sizeof( float ) : 0;   /* E178: + the sun */
		const VkDeviceSize bytes = gridBytes + fieldBytes;

		vk.rt.world.haveLightField = qfalse;
		/* [QL] E194: in GPU memory, staged once. It is written at map load and
		   then only read - by every shadow ray, several times each - and in
		   host-visible memory every one of those reads crossed the PCIe bus.
		   System memory stays as the fallback for a card that is out of room. */
		if ( rt_create_buffer( bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				&vk.rt.world.grid_buffer, &vk.rt.world.grid_memory ) ) {
			vk.rt.world.grid_ptr = NULL;
			if ( points > 0 ) {
				rt_upload_at( vk.rt.world.grid_buffer, 0, world->lightGridData, gridBytes );
				if ( fieldBytes ) {
					rt_upload_at( vk.rt.world.grid_buffer, gridBytes, world->lightField, fieldBytes );
					vk.rt.world.haveLightField = qtrue;
				}
				vk.rt.world.haveGrid = qtrue;
			} else {
				static const byte zero[16];
				rt_upload_at( vk.rt.world.grid_buffer, 0, zero, sizeof( zero ) );
			}
			ri.Printf( PRINT_ALL, "RT: light grid%s in GPU memory (%i KiB)\n",
				vk.rt.world.haveLightField ? " and light field" : "", (int)( bytes / 1024 ) );
		} else if ( rt_create_host_buffer( bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				&vk.rt.world.grid_buffer, &vk.rt.world.grid_memory, &vk.rt.world.grid_ptr ) ) {
			if ( points > 0 ) {
				Com_Memcpy( vk.rt.world.grid_ptr, world->lightGridData, (size_t)gridBytes );
				if ( fieldBytes ) {
					Com_Memcpy( (byte *)vk.rt.world.grid_ptr + gridBytes, world->lightField, (size_t)fieldBytes );
					vk.rt.world.haveLightField = qtrue;
				}
				vk.rt.world.haveGrid = qtrue;
			} else {
				Com_Memset( vk.rt.world.grid_ptr, 0, (size_t)bytes );
			}
			ri.Printf( PRINT_WARNING, "RT: light grid in system memory (%i KiB) - no room in GPU memory\n",
				(int)( bytes / 1024 ) );
		} else {
			ri.Printf( PRINT_WARNING, "RT: no light grid buffer - reflections will not ray trace\n" );
		}
	}

	/* The AO descriptor names this TLAS, so it has to be rewritten whenever the
	   structure is rebuilt - which is every map load. Pointing at the destroyed
	   one from the previous map is a use-after-free the validation layers catch
	   and a driver may not. */
	vk_rt_update_ao_descriptor();
	vk_ssr_update_rt_descriptor();   /* [QL] E156, the same for the reflection */
	vk_rt_update_main_descriptor();  /* [QL] E161, and for the main pass's shadow rays */
	vk_actor_shadow_update_descriptor();   /* [QL] E166 */

	ri.Printf( PRINT_ALL, "RT: world acceleration structure ready%s\n",
		vk.rt.world.dynReady ? " (+ dynamic entities)" : "" );
	vk_print_memory( qtrue );   /* [QL] E194: with the map's structures in place */
}
