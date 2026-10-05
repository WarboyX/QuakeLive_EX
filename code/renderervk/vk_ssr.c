/*
===========================================================================
[QL] vk_ssr.c - water: the water planes and walls found at map load (R19,
E144), and the reflection pass drawn on them (R19 onwards). Split out of
vk.c unchanged.
===========================================================================
*/
#include "vk_local.h"



/*
=================
vk_find_water_walls

[QL] E144. The faces a ripple bounces off - see vkWaterWall_t.

For each water plane: every face that is drawn and solid (not liquid, fog,
sky or nodraw), no flatter than about 15 degrees - a sheer wall or a sloping
bank - that reaches from below the water's height to above it (give or take a
couple of units, for a lip that stops exactly at the surface) beside the
water's box. What is kept is where the face cuts that height: a segment of
shoreline, not the face's box, which for a long sloping bank reaches far past
the water line in xy. Faces only, like the planes: a curved patch wall has no
single line to reflect across.

Then panels of one wall - same line, touching - become one segment, and
anything shorter than VK_WATER_WALL_MIN is dropped (vkWaterWall_t says why).
=================
*/
static void vk_find_water_walls( const world_t *world )
{
	const int skip = CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_FOG;
	int i, j, k, merged, perPlane[ VK_MAX_WATER_PLANES ];
	qboolean full = qfalse;

	vk.numWaterWalls = 0;

	for ( i = 0; i < world->numsurfaces; i++ ) {
		const msurface_t *surf = &world->surfaces[i];
		const srfSurfaceFace_t *face;
		vec3_t mins, maxs;
		float len;

		if ( surf->data == NULL || surf->shader == NULL || *surf->data != SF_FACE ) {
			continue;
		}
		if ( ( surf->shader->contentFlags & skip ) ||
			 ( surf->shader->surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) ) {
			continue;
		}
		face = (const srfSurfaceFace_t *)surf->data;
		len = sqrtf( face->plane.normal[0] * face->plane.normal[0] + face->plane.normal[1] * face->plane.normal[1] );
		if ( len < 0.26f ) {
			continue;   // flatter than about 15 degrees: a floor, not a shore
		}
		ClearBounds( mins, maxs );
		for ( k = 0; k < face->numPoints; k++ ) {
			AddPointToBounds( face->points[k], mins, maxs );
		}

		for ( j = 0; j < vk.numWaterPlanes; j++ ) {
			const vkWaterPlane_t *wp = &vk.waterPlanes[j];
			const float h = wp->dist;   // up-facing planes: the height
			float nx, ny;

			if ( mins[2] > h + 2.0f || maxs[2] < h - 2.0f ) {
				continue;   // does not cross this water's height
			}
			if ( maxs[0] < wp->mins[0] - 8.0f || mins[0] > wp->maxs[0] + 8.0f ||
				 maxs[1] < wp->mins[1] - 8.0f || mins[1] > wp->maxs[1] + 8.0f ) {
				continue;   // nowhere near it
			}
			nx = face->plane.normal[0] / len;
			ny = face->plane.normal[1] / len;

			/*
			Triangle by triangle, not round the face's outline: the points of a
			face are its draw vertices, and a q3map2 -meta face is a batch of
			coplanar triangles whose vertices are in no particular order and
			whose outline need not even be convex. Walking them as a polygon
			produced a few segments per pond and lost most of the sloped bank.
			Each triangle that crosses the water gives one short piece of
			shoreline; the merge below joins the pieces of one face (same line)
			back into the wall they were cut from.
			*/
			{
				const unsigned *ind = (const unsigned *)( (const byte *)face + face->ofsIndices );
				int t;

				for ( t = 0; t + 2 < face->numIndices; t += 3 ) {
					float lo = 1e9f, hi = -1e9f;
					vkWaterWall_t *w;
					int e;

					for ( e = 0; e < 3; e++ ) {
						const float *p0 = face->points[ ind[ t + e ] ];
						const float *p1 = face->points[ ind[ t + ( e + 1 ) % 3 ] ];
						float u;

						/* where the triangle meets the water: its edges crossing
						   h, and any corner within the couple of units allowed
						   for a lip, measured along the line (tangent -ny, nx) */
						if ( fabsf( p0[2] - h ) <= 2.0f ) {
							u = -ny * p0[0] + nx * p0[1];
							lo = MIN( lo, u ); hi = MAX( hi, u );
						}
						if ( ( p0[2] - h ) * ( p1[2] - h ) < 0.0f ) {
							float f = ( h - p0[2] ) / ( p1[2] - p0[2] );
							u = -ny * ( p0[0] + ( p1[0] - p0[0] ) * f ) + nx * ( p0[1] + ( p1[1] - p0[1] ) * f );
							lo = MIN( lo, u ); hi = MAX( hi, u );
						}
					}
					if ( lo > hi ) {
						continue;   // this triangle is all above or all below
					}
					if ( vk.numWaterWalls >= VK_MAX_WATER_WALLS ) {
						full = qtrue;
						break;
					}

					w = &vk.waterWalls[ vk.numWaterWalls++ ];
					w->n[0] = nx;
					w->n[1] = ny;
					// the face's plane cut at the water's height, as a line in xy
					w->d = ( face->plane.dist - face->plane.normal[2] * h ) / len;
					w->a[0] = nx * w->d - ny * lo; w->a[1] = ny * w->d + nx * lo;
					w->b[0] = nx * w->d - ny * hi; w->b[1] = ny * w->d + nx * hi;
					/* a sloping bank still echoes the wave, a little less than a
					   sheer wall - part of it runs up the slope - so the weight
					   goes from 1 at vertical to 0.6 at the flattest bank taken,
					   about 15 degrees */
					w->weight = 0.6f + 0.4f * ( len - 0.26f ) / ( 1.0f - 0.26f );
					w->plane = j;
				}
			}
		}
	}

	/* one wall built from several panels is one wall: same plane of water,
	   same line, segments touching - join them, until nothing more joins */
	do {
		merged = 0;
		for ( i = 0; i < vk.numWaterWalls; i++ ) {
			vkWaterWall_t *w = &vk.waterWalls[i];
			const float tx = -w->n[1], ty = w->n[0];

			for ( j = i + 1; j < vk.numWaterWalls; j++ ) {
				vkWaterWall_t *o = &vk.waterWalls[j];
				float wa, wb, oa, ob, lo, hi;

				if ( o->plane != w->plane || w->n[0] * o->n[0] + w->n[1] * o->n[1] < 0.999f ||
					 fabsf( o->d - w->d ) > 1.0f ) {
					continue;
				}
				wa = tx * w->a[0] + ty * w->a[1]; wb = tx * w->b[0] + ty * w->b[1];
				oa = tx * o->a[0] + ty * o->a[1]; ob = tx * o->b[0] + ty * o->b[1];
				if ( MIN( oa, ob ) > MAX( wa, wb ) + 2.0f || MAX( oa, ob ) < MIN( wa, wb ) - 2.0f ) {
					continue;   // same line, but a gap between them
				}
				lo = MIN( MIN( wa, wb ), MIN( oa, ob ) );
				hi = MAX( MAX( wa, wb ), MAX( oa, ob ) );
				w->a[0] = w->n[0] * w->d + tx * lo; w->a[1] = w->n[1] * w->d + ty * lo;
				w->b[0] = w->n[0] * w->d + tx * hi; w->b[1] = w->n[1] * w->d + ty * hi;
				w->weight = MIN( w->weight, o->weight );
				*o = vk.waterWalls[ --vk.numWaterWalls ];
				j--;
				merged++;
			}
		}
	} while ( merged );

	Com_Memset( perPlane, 0, sizeof( perPlane ) );
	for ( i = 0; i < vk.numWaterWalls; ) {
		const vkWaterWall_t *w = &vk.waterWalls[i];
		if ( hypotf( w->b[0] - w->a[0], w->b[1] - w->a[1] ) < VK_WATER_WALL_MIN ) {
			vk.waterWalls[i] = vk.waterWalls[ --vk.numWaterWalls ];   // a post, not a shore
			continue;
		}
		perPlane[ w->plane ]++;
		i++;
	}

	for ( j = 0; j < vk.numWaterPlanes; j++ ) {
		ri.Printf( PRINT_ALL, "  %2i: %i wall(s) for ripples to bounce off\n", j, perPlane[j] );
	}
	if ( full ) {
		ri.Printf( PRINT_WARNING, "Water: hit the ceiling of %i wall faces - some ripples will not "
			"bounce\n", VK_MAX_WATER_WALLS );
	}
}


/*
=================
vk_find_water_planes

[QL] The distinct liquid planes in the map, collected once at load.

Anything that reflects off water has to answer "is this pixel water, and what
plane is it on" per pixel. Answering it per surface means marking water at draw
time - new pipeline state, or a stencil bit, and a second reason for every water
surface to be special. Answering it from the plane costs a handful of floats and
a compare, because a map has a few water heights and not a few thousand.

Up-facing only. A water volume has sides and a bottom too and neither reflects
anything; the surface you see from above is the one that does. 0.7 is about 45
degrees, which keeps a sloped waterfall lip and rejects the walls.

Faces only. Water in these maps is flat brushwork - a patch mesh would have no
single plane to collect, and forcing one would put a reflection plane through
the middle of a curve.

Reported even when it finds nothing, because "nothing" is the answer that says
this approach does not fit the map, and that is worth one line at load rather
than a reflection pass that silently never runs.
=================
*/
/* [QL] K2: every water face is a candidate; the largest VK_MAX_WATER_PLANES win */
#define WATER_CANDIDATES	1024
typedef struct {
	vkWaterPlane_t	plane;
	float			area;
	int				order;
} waterCand_t;

static int vk_water_cand_cmp( const void *a, const void *b )
{
	const waterCand_t *x = (const waterCand_t *)a, *y = (const waterCand_t *)b;
	if ( x->area != y->area ) {
		return x->area > y->area ? -1 : 1;
	}
	return x->order - y->order;
}

void vk_find_water_planes( const world_t *world )
{
	const int liquid = CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA;
	int surfaces = 0, waterSurfaces = 0;
	vec3_t surfMins, surfMaxs;
	int i, j, k;
	static waterCand_t cand[WATER_CANDIDATES];
	int numCand = 0, candDropped = 0;
	float areaDropped = 0.0f;

	vk.numWaterPlanes = 0;

	if ( world == NULL || world->surfaces == NULL || world->numsurfaces <= 0 ) {
		return;
	}

	for ( i = 0; i < world->numsurfaces; i++ ) {
		const msurface_t *surf = &world->surfaces[i];
		const srfSurfaceFace_t *face;
		qboolean known;

		if ( surf->data == NULL || surf->shader == NULL ) {
			continue;
		}
		if ( !( surf->shader->contentFlags & liquid ) ) {
			continue;
		}
		surfaces++;

		if ( *surf->data != SF_FACE ) {
			continue;   // a patch has no one plane to stand for it
		}
		if ( !( surf->shader->contentFlags & CONTENTS_WATER ) ) {
			continue;   // lava and slime are counted, not reflected
		}
		face = (const srfSurfaceFace_t *)surf->data;

		if ( face->plane.normal[2] < 0.7f ) {
			continue;   // a wall or the underside, not a surface to look at
		}
		waterSurfaces++;

		/*
		[QL] The bounds of this face, so the plane can be bounded by them.

		A plane is infinite and a pool is not. Testing against the plane alone
		matches every surface in the map at the water's height - which on this
		map is the wooden floor around the pool, and it shows up as bands and
		speckles along the height lines rather than as anything that looks like
		a mistake about water.
		*/
		ClearBounds( surfMins, surfMaxs );
		for ( k = 0; k < face->numPoints; k++ ) {
			AddPointToBounds( face->points[k], surfMins, surfMaxs );
		}

		/*
		One record per face, and deliberately no merging.

		Merging faces whose boxes touch looked tidy and is wrong in the way that
		matters: a water brush usually continues under the decking around the
		pool, those faces touch the visible ones, and the merged box therefore
		covers ground the water cannot be seen on. The box is the only thing
		bounding an infinite plane, so every unit it gains is a unit of floor
		the mask can spill onto - which is what "the mask does not stop at the
		pool edge" is.

		A duplicate face costs one more iteration of a loop over a handful of
		planes. A box larger than the water costs correctness.

		Still skipped when an identical face is already recorded, because BSP
		splitting can hand back the same rectangle twice and there is nothing to
		be gained from holding it twice.
		*/
		known = qfalse;
		for ( j = 0; j < numCand; j++ ) {
			const vkWaterPlane_t *wp = &cand[j].plane;

			if ( fabsf( wp->dist - face->plane.dist ) >= 1.0f ||
				 DotProduct( wp->normal, face->plane.normal ) <= 0.999f ) {
				continue;
			}
			if ( VectorCompare( wp->mins, surfMins ) && VectorCompare( wp->maxs, surfMaxs ) ) {
				known = qtrue;
				break;
			}
		}
		if ( known ) {
			continue;
		}
		if ( numCand >= WATER_CANDIDATES ) {
			candDropped++;
			continue;
		}

		VectorCopy( face->plane.normal, cand[numCand].plane.normal );
		cand[numCand].plane.dist = face->plane.dist;
		VectorCopy( surfMins, cand[numCand].plane.mins );
		VectorCopy( surfMaxs, cand[numCand].plane.maxs );
		cand[numCand].area = ( surfMaxs[0] - surfMins[0] ) * ( surfMaxs[1] - surfMins[1] );
		cand[numCand].order = numCand;
		numCand++;
	}

	/*
	[QL] K2: the largest faces, not the first ones.

	Past VK_MAX_WATER_PLANES the faces used to be taken in BSP order, so
	which water reflected depended on how the compiler split the map - a
	puddle early in the lump could cost the main pool its reflection. Every
	candidate is collected first and the largest (by the box the shader tests,
	which is what it covers on screen) are kept; BSP order breaks ties, so the
	choice is the same every load.
	*/
	if ( numCand > VK_MAX_WATER_PLANES ) {
		qsort( cand, numCand, sizeof( cand[0] ), vk_water_cand_cmp );
		for ( j = VK_MAX_WATER_PLANES; j < numCand; j++ ) {
			areaDropped += cand[j].area;
		}
	}
	for ( j = 0; j < numCand && j < VK_MAX_WATER_PLANES; j++ ) {
		vk.waterPlanes[j] = cand[j].plane;
	}
	vk.numWaterPlanes = j;

	if ( surfaces == 0 ) {
		ri.Printf( PRINT_ALL, "Water: this map has no liquid surfaces\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Water: %i reflector(s) from %i up-facing water surface(s), "
		"%i liquid surface(s) in all\n",
		vk.numWaterPlanes, waterSurfaces, surfaces );

	for ( i = 0; i < vk.numWaterPlanes; i++ ) {
		ri.Printf( PRINT_ALL, "  %2i: z %.0f, %.0f x %.0f units, from %.0f %.0f to %.0f %.0f\n", i,
			vk.waterPlanes[i].dist,
			vk.waterPlanes[i].maxs[0] - vk.waterPlanes[i].mins[0],
			vk.waterPlanes[i].maxs[1] - vk.waterPlanes[i].mins[1],
			vk.waterPlanes[i].mins[0], vk.waterPlanes[i].mins[1],
			vk.waterPlanes[i].maxs[0], vk.waterPlanes[i].maxs[1] );
	}

	if ( numCand > VK_MAX_WATER_PLANES || candDropped ) {
		ri.Printf( PRINT_WARNING, "Water: %i water faces, room for %i - the largest kept; %i left out "
			"(%.0f square units)%s\n", numCand + candDropped, VK_MAX_WATER_PLANES,
			numCand - vk.numWaterPlanes + candDropped, areaDropped,
			candDropped ? ", some never ranked: the candidate list was full" : "" );
	}

	vk_find_water_walls( world );
}

/*
=================
[QL] R19: screen-space reflections on the map's water planes.

Two passes, and the reason is a Vulkan rule rather than a preference: the trace
samples the scene colour and the composite writes to it, and a pass cannot read
the attachment it is writing. So the trace resolves into vk.ssr.image with alpha
carrying how much of the result to keep, and the composite is an ordinary
source-alpha blend of that image over the scene. The water test therefore lives
in one shader instead of two, with nothing to keep in step.

The composite borrows vk.render_pass.rtao. Render pass compatibility is by
attachment count, format and sample count, and that pass was built against the
same main framebuffer - so it needs no pass or framebuffer of its own, and the
blend state is the only thing that differs from the occlusion composite.

Every failure disables the feature with a reason and leaves the rest of the
renderer alone.
=================
*/

#define SSR_MAX_PLANES VK_MAX_WATER_PLANES
#define SSR_MAX_RIPPLES MAX_WATER_RIPPLES   /* [QL] R19, and the shader's copy */
/* [QL] E148: RIPPLE_REFLECT_MIN is gone. Only splashes of strength 1.2 or
   more used to bounce, so a bullet landing beside a wall never echoed - and
   physically a wave's size has nothing to do with whether a wall returns it.
   Every splash bounces now; what limits it is distance, in vk_ripple_walls
   (a wall within half the splash's reach), so a small splash echoes only
   when it lands near the edge, as a real one does. */

/* [QL] R28: SSR_RIPPLE_LIFE was 2.2f here and RIPPLE_LIFE 2.2 in ssr.tmpl, with
   a comment on each telling the next person to keep them equal. They are now one
   value, r_waterRippleLife, sent in rippleTune.x - so the comment is unnecessary
   rather than merely obeyed, and the constant is gone instead of left unread. */
#define SSR_MAX_LIGHTS 16                   /* and the shader's copy */

typedef struct {
	float viewProj[16];
	float invViewProj[16];
	float eye[4];
	float params[4];      // strength, distance, steps, thickness
	float depthInfo[4];   // cleared depth, weapon band, sign, r_ssrDebug
	float planes[SSR_MAX_PLANES][4];
	float boundsMin[SSR_MAX_PLANES][4];
	float boundsMax[SSR_MAX_PLANES][4];
	float planeCount[4];
	float wave[4];        // slope, units per wavelength, speed, seconds
	float wave2[4];       // height in units, foam; z..w spare
	float rippleTune[4];  // [QL] R28: life, waves, height scale, size scale
	float ripple[SSR_MAX_RIPPLES][4];    // xy where, z age, w reach
	float ripple2[SSR_MAX_RIPPLES][4];   // x strength, y plane index, z bounce, w walls
	float ripple3[SSR_MAX_RIPPLES][4];   // [QL] E144: mirror images 0 and 1, xy xy
	float ripple4[SSR_MAX_RIPPLES][4];   // [QL] E144: mirror images 2 and 3
	float ripple5[SSR_MAX_RIPPLES][4];   // [QL] E144: how much each image sends back
	float ripple6[SSR_MAX_RIPPLES][4];   // [QL] E148: x ring tightening; yzw spare
	float emitter[SSR_MAX_LIGHTS][4];    // xyz world, w radius
	float emitter2[SSR_MAX_LIGHTS][4];   // rgb colour, a intensity
	float rtInfo[4];      // [QL] E156: r_ssrRayTrace, ray length, albedo; w spare
	float gridOrigin[4];  // [QL] E156: light grid origin, w 1 if there is one
	float gridInvSize[4]; // [QL] E156: 1 / spacing
	float gridBounds[4];  // [QL] E156: points per axis
} ssrUniform_t;

/*
[QL] E144. The walls a ripple bounces off, as mirror images of its centre.

Up to VK_MAX_RIPPLE_WALLS of its water's wall faces (vk_find_water_walls),
nearest first, that it faces (the impact on the water side of the wall), that
its rings can reach and come back from (within half its reach), and that are
really there opposite it - the foot of the perpendicular from the impact
lands on the wall's shoreline segment. A wall section beside the pool but not across
from the splash is not what the wave hits. Two faces on the same line (a wall
split into panels) are one wall.
*/
static void vk_ripple_walls( const float *c, float reach, int plane, ssrUniform_t *u, int slot )
{
	float best[ VK_MAX_RIPPLE_WALLS ][4];   // s, image x, image y, weight
	int n = 0, i, j, k;

	for ( i = 0; i < vk.numWaterWalls; i++ ) {
		const vkWaterWall_t *w = &vk.waterWalls[i];
		float s, fx, fy;

		if ( w->plane != plane ) {
			continue;
		}
		s = w->n[0] * c[0] + w->n[1] * c[1] - w->d;
		if ( s <= 0.5f || s > reach * 0.5f ) {
			continue;   // behind it, or too far for anything to come back
		}
		/* where the perpendicular from the splash lands, along the wall's
		   segment - on it, or within a few units of an end */
		fx = -w->n[1] * ( c[0] - w->a[0] ) + w->n[0] * ( c[1] - w->a[1] );
		fy = -w->n[1] * ( w->b[0] - w->a[0] ) + w->n[0] * ( w->b[1] - w->a[1] );
		if ( fx < MIN( 0.0f, fy ) - 8.0f || fx > MAX( 0.0f, fy ) + 8.0f ) {
			continue;   // not the stretch of shore across from the splash
		}
		for ( j = 0; j < n; j++ ) {
			if ( fabsf( best[j][0] - s ) < 4.0f &&
				 fabsf( best[j][1] - ( c[0] - 2.0f * s * w->n[0] ) ) < 4.0f &&
				 fabsf( best[j][2] - ( c[1] - 2.0f * s * w->n[1] ) ) < 4.0f ) {
				break;  // the same wall again
			}
		}
		if ( j < n ) {
			continue;
		}
		if ( n < VK_MAX_RIPPLE_WALLS ) {
			j = n++;
		} else {
			j = 0;
			for ( k = 1; k < n; k++ ) {
				if ( best[k][0] > best[j][0] ) {
					j = k;
				}
			}
			if ( best[j][0] <= s ) {
				continue;
			}
		}
		best[j][0] = s;
		best[j][1] = c[0] - 2.0f * s * w->n[0];
		best[j][2] = c[1] - 2.0f * s * w->n[1];
		best[j][3] = w->weight;
	}
	for ( i = 0; i < n; i++ ) {
		float *dst = i < 2 ? u->ripple3[slot] : u->ripple4[slot];
		dst[( i & 1 ) * 2 + 0] = best[i][1];
		dst[( i & 1 ) * 2 + 1] = best[i][2];
		u->ripple5[slot][i] = best[i][3];
	}
	u->ripple2[slot][3] = (float)n;
}

static qboolean ssrReported = qfalse;
static int ssrRippleReport = 0;   /* [QL] rate limit for the drop report below */

void vk_ssr_destroy( void )
{
	uint32_t i;

	vk.ssr.ready = qfalse;

	if ( vk.ssr.trace_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.trace_pipeline, NULL );
		vk.ssr.trace_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.ssr.composite_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.composite_pipeline, NULL );
		vk.ssr.composite_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.ssr.debug_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.debug_pipeline, NULL );
		vk.ssr.debug_pipeline = VK_NULL_HANDLE;
	}
	/* [QL] E151 */
	if ( vk.ssr.trace_pipeline_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.trace_pipeline_half, NULL );
		vk.ssr.trace_pipeline_half = VK_NULL_HANDLE;
	}
	if ( vk.ssr.composite_pipeline_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.composite_pipeline_half, NULL );
		vk.ssr.composite_pipeline_half = VK_NULL_HANDLE;
	}
	if ( vk.ssr.debug_pipeline_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.debug_pipeline_half, NULL );
		vk.ssr.debug_pipeline_half = VK_NULL_HANDLE;
	}
	/* [QL] E156: the ray-traced variant's own pieces */
	vk.ssr.rtReady = qfalse;
	if ( vk.ssr.rt_trace_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.rt_trace_pipeline, NULL );
		vk.ssr.rt_trace_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.ssr.rt_trace_pipeline_half != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.ssr.rt_trace_pipeline_half, NULL );
		vk.ssr.rt_trace_pipeline_half = VK_NULL_HANDLE;
	}
	if ( vk.ssr.rt_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.ssr.rt_pipeline_layout, NULL );
		vk.ssr.rt_pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.ssr.rt_pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.ssr.rt_pool, NULL );
		vk.ssr.rt_pool = VK_NULL_HANDLE;
		Com_Memset( vk.ssr.rt_descriptor, 0, sizeof( vk.ssr.rt_descriptor ) );
	}
	if ( vk.ssr.rt_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.ssr.rt_set_layout, NULL );
		vk.ssr.rt_set_layout = VK_NULL_HANDLE;
	}

	if ( vk.ssr.trace_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.ssr.trace_pipeline_layout, NULL );
		vk.ssr.trace_pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.ssr.composite_pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.ssr.composite_pipeline_layout, NULL );
		vk.ssr.composite_pipeline_layout = VK_NULL_HANDLE;
	}

	for ( i = 0; i < ARRAY_LEN( vk.ssr.uniform_buffer ); i++ ) {
		if ( vk.ssr.uniform_buffer[i] != VK_NULL_HANDLE ) {
			qvkUnmapMemory( vk.device, vk.ssr.uniform_memory[i] );
			qvkDestroyBuffer( vk.device, vk.ssr.uniform_buffer[i], NULL );
			qvkFreeMemory( vk.device, vk.ssr.uniform_memory[i], NULL );
			vk.ssr.uniform_buffer[i] = VK_NULL_HANDLE;
			vk.ssr.uniform_memory[i] = VK_NULL_HANDLE;
			vk.ssr.uniform_ptr[i] = NULL;
		}
	}

	/* the sets go with the pool */
	if ( vk.ssr.pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.ssr.pool, NULL );
		vk.ssr.pool = VK_NULL_HANDLE;
		Com_Memset( vk.ssr.trace_descriptor, 0, sizeof( vk.ssr.trace_descriptor ) );
		vk.ssr.composite_descriptor = VK_NULL_HANDLE;
	}
	if ( vk.ssr.trace_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.ssr.trace_set_layout, NULL );
		vk.ssr.trace_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.ssr.composite_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.ssr.composite_set_layout, NULL );
		vk.ssr.composite_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.ssr.sampler != VK_NULL_HANDLE ) {
		qvkDestroySampler( vk.device, vk.ssr.sampler, NULL );
		vk.ssr.sampler = VK_NULL_HANDLE;
	}
	/* the framebuffer and the image belong to the attachment set and are
	   destroyed with it; the render pass is destroyed with the others */
	ssrReported = qfalse;
}

void vk_ssr_create_render_pass( VkDevice device )
{
	VkAttachmentDescription attachment;
	VkAttachmentReference colorRef;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo desc;

	/*
	[QL] vk.ssr.format is chosen in vk_create_attachments, which runs *before*
	this - that order is the whole reason this is not decided here.

	It was, for one build, and the feature disabled itself on every launch: the
	image is created with the attachments, so by the time this function had
	picked a format the image it was picking for had already been created, or
	rather skipped, because it was guarded on this pass existing and this pass
	does not exist yet. "SSR: not running - the pass was not created", from a
	function whose only job is to create it.

	UNDEFINED means vk_create_attachments found no target that can carry alpha
	and said so. No pass, and vk_ssr_create reports it.
	*/
	if ( vk.ssr.format == VK_FORMAT_UNDEFINED ) {
		return;
	}

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = vk.ssr.format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	/*
	[QL] Cleared, and this is not the cheap-versus-safe trade it looks like.

	It was DONT_CARE, on the grounds that the trace writes every pixel: it
	assigns out_color before any return, so every path leaves a value. That
	reasoning is about the shader, and the shader is not the only thing that
	decides whether a pixel is written - the render area, the viewport and the
	scissor do too, and any pixel none of them reach keeps whatever was in that
	memory.

	Uninitialised memory here does not read as noise, because the composite
	blends by the alpha it finds. Garbage alpha is almost never zero, so the
	discard does not fire and the garbage *colour* goes on screen: font-atlas
	glyphs and old framebuffer contents appearing on the water, effects spilling
	past the pool onto the decking, the debug views washing over the whole
	screen, all of it fine immediately after a vid_restart while the memory is
	still fresh and degrading from there as the rest of the renderer churns it.

	Clearing to zero makes "not written" mean alpha 0, which is the one value
	the composite already treats as nothing to do. One clear of one
	screen-sized target per frame, against a class of bug that cannot be
	reasoned about from the shader at all.
	*/
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	Com_Memset( deps, 0, sizeof( deps ) );
	/* Do not start overwriting this while last frame's composite is still
	   reading it. */
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	/* And let the composite read what this wrote. Not BY_REGION - the trace
	   marches sideways across the screen, so a tile's result depends on pixels
	   outside it. */
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.attachmentCount = 1;
	desc.pAttachments = &attachment;
	desc.subpassCount = 1;
	desc.pSubpasses = &subpass;
	desc.dependencyCount = 2;
	desc.pDependencies = deps;

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.ssr.offscreen_pass ) );
	SET_OBJECT_NAME( vk.ssr.offscreen_pass, "render pass - ssr offscreen", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
}


/*
[QL] E156: point the ray-traced sets at this map's acceleration structure and
light grid. Called at world build, and at the end of vk_ssr_create for a
vid_restart with a map already loaded; either may come first.
*/
void vk_ssr_update_rt_descriptor( void )
{
	uint32_t n;

	vk.ssr.rtReady = qfalse;

	if ( vk.ssr.rt_pool == VK_NULL_HANDLE || !vk.rt.world.worldBuilt ||
		vk.rt.world.tlas == VK_NULL_HANDLE || vk.rt.world.grid_buffer == VK_NULL_HANDLE ) {
		return;
	}

	for ( n = 0; n < ARRAY_LEN( vk.ssr.rt_descriptor ); n++ ) {
		VkAccelerationStructureKHR as = vk.rt.world.dynReady
			? vk.rt.world.dyn_tlas[n] : vk.rt.world.tlas;
		VkWriteDescriptorSetAccelerationStructureKHR as_info;
		VkDescriptorBufferInfo grid_info;
		VkWriteDescriptorSet writes[2];

		if ( as == VK_NULL_HANDLE ) {
			return;
		}

		Com_Memset( &as_info, 0, sizeof( as_info ) );
		as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		as_info.accelerationStructureCount = 1;
		as_info.pAccelerationStructures = &as;

		Com_Memset( &grid_info, 0, sizeof( grid_info ) );
		grid_info.buffer = vk.rt.world.grid_buffer;
		grid_info.range = VK_WHOLE_SIZE;

		Com_Memset( writes, 0, sizeof( writes ) );
		/* see vk_rt_update_ao_descriptor: the structure rides in pNext */
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].pNext = &as_info;
		writes[0].dstSet = vk.ssr.rt_descriptor[n];
		writes[0].dstBinding = 3;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = vk.ssr.rt_descriptor[n];
		writes[1].dstBinding = 4;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = &grid_info;

		qvkUpdateDescriptorSets( vk.device, 2, writes, 0, NULL );
	}

	vk.ssr.rtReady = qtrue;
	ri.Printf( PRINT_ALL, "SSR: ray-traced fallback ready (%s)\n",
		vk.rt.world.haveGrid ? "light grid for off-screen hits" : "no light grid - on-screen hits only" );
}


/*
[QL] E156: the ray-traced variant of the trace, on top of a working pass.
Failure here only loses r_ssrRayTrace - the plain march keeps running.
*/
static void vk_ssr_create_rt( void )
{
	VkDescriptorSetLayoutBinding bindings[5];
	VkDescriptorSetLayoutCreateInfo layout_desc;
	VkDescriptorPoolSize pool_sizes[4];
	VkDescriptorPoolCreateInfo pool_desc;
	VkDescriptorSetAllocateInfo set_alloc;
	VkPipelineLayoutCreateInfo pl_desc;
	VkResult res;
	uint32_t i;

	if ( !vk.rtActive || vk.modules.ssr_rt_fs == VK_NULL_HANDLE || vk.modules.ssr_rt_ms_fs == VK_NULL_HANDLE ) {
		return;
	}

	/* 0-2 exactly as the plain trace's, then the structure and the grid */
	Com_Memset( bindings, 0, sizeof( bindings ) );
	for ( i = 0; i < 5; i++ ) {
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
	layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_desc.bindingCount = 5;
	layout_desc.pBindings = bindings;

	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.ssr.rt_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: ray-traced set layout failed (%s) - march only\n", vk_result_string( res ) );
		return;
	}

	Com_Memset( pool_sizes, 0, sizeof( pool_sizes ) );
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = 2 * NUM_COMMAND_BUFFERS;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[1].descriptorCount = NUM_COMMAND_BUFFERS;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[2].descriptorCount = NUM_COMMAND_BUFFERS;
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[3].descriptorCount = NUM_COMMAND_BUFFERS;

	Com_Memset( &pool_desc, 0, sizeof( pool_desc ) );
	pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_desc.maxSets = NUM_COMMAND_BUFFERS;
	pool_desc.poolSizeCount = 4;
	pool_desc.pPoolSizes = pool_sizes;

	res = qvkCreateDescriptorPool( vk.device, &pool_desc, NULL, &vk.ssr.rt_pool );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: ray-traced descriptor pool failed (%s) - march only\n", vk_result_string( res ) );
		return;
	}

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.ssr.rt_pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.ssr.rt_set_layout;

	for ( i = 0; i < ARRAY_LEN( vk.ssr.rt_descriptor ); i++ ) {
		VkDescriptorImageInfo image_info[2];
		VkDescriptorBufferInfo buffer_info;
		VkWriteDescriptorSet writes[3];

		res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.ssr.rt_descriptor[i] );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "SSR: ray-traced set %i failed (%s) - march only\n", i, vk_result_string( res ) );
			return;
		}

		/* the same three the plain trace set carries - see there for the
		   layouts, which are the whole of two old bugs */
		Com_Memset( image_info, 0, sizeof( image_info ) );
		image_info[0].sampler = vk.rt.depth_sampler;
		image_info[0].imageView = vk.rt.depth_view;
		image_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_info[1].sampler = vk.ssr.sampler;
		image_info[1].imageView = vk.color_image_view;
		image_info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		Com_Memset( &buffer_info, 0, sizeof( buffer_info ) );
		buffer_info.buffer = vk.ssr.uniform_buffer[i];
		buffer_info.range = sizeof( ssrUniform_t );

		Com_Memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = vk.ssr.rt_descriptor[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &image_info[0];

		writes[1] = writes[0];
		writes[1].dstBinding = 1;
		writes[1].pImageInfo = &image_info[1];

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = vk.ssr.rt_descriptor[i];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[2].pBufferInfo = &buffer_info;

		qvkUpdateDescriptorSets( vk.device, 3, writes, 0, NULL );
	}

	Com_Memset( &pl_desc, 0, sizeof( pl_desc ) );
	pl_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_desc.setLayoutCount = 1;
	pl_desc.pSetLayouts = &vk.ssr.rt_set_layout;

	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.ssr.rt_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: ray-traced pipeline layout failed (%s) - march only\n", vk_result_string( res ) );
		return;
	}

	vk_create_post_process_pipeline( 18, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 19, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );

	/* a map already loaded (vid_restart mid-match): point at it now */
	vk_ssr_update_rt_descriptor();
}


void vk_ssr_create( void )
{
	VkDescriptorSetLayoutBinding bindings[3];
	VkDescriptorSetLayoutCreateInfo layout_desc;
	VkDescriptorPoolSize pool_sizes[2];
	VkDescriptorPoolCreateInfo pool_desc;
	VkDescriptorSetAllocateInfo set_alloc;
	VkPipelineLayoutCreateInfo pl_desc;
	VkSamplerCreateInfo sampler_desc;
	VkResult res;
	uint32_t i;

	vk_ssr_destroy();

	/*
	[QL] Say which piece is missing, because they fail for different reasons and
	the difference is the whole diagnosis.

	This was one silent return and r_ssr reported "the pass was not created",
	which sent a round of work at the render pass function - and the render pass
	was fine. The image had not been created, because the format it needed was
	being chosen after the image was built. Three words of log would have named
	it immediately.
	*/
	if ( vk.ssr.offscreen_pass == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "SSR: no offscreen render pass%s - disabling\n",
			vk.ssr.format == VK_FORMAT_UNDEFINED
				? " (no colour target on this device carries alpha)" : "" );
		return;
	}
	if ( vk.ssr.framebuffer == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "SSR: the render pass exists but its image does not - "
			"disabling. The target is built with the attachments, before the render "
			"passes; anything it depends on has to be decided by then.\n" );
		return;
	}
	if ( vk.render_pass.rtao == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "SSR: no composite render pass - disabling\n" );
		return;
	}
	if ( vk.rt.depth_view == VK_NULL_HANDLE || vk.color_image_view == VK_NULL_HANDLE ) {
		/* depth_view is the depth-aspect-only view the occlusion pass makes. It
		   is created whether or not ray tracing is on, but not if the renderer
		   decided depth is not sampleable - which is a real device limit and
		   the one case this cannot work around. */
		return;
	}

	/* Linear and clamped. The march lands between texels and the fade at the
	   screen edge is computed from the coordinate, so wrapping there would put
	   the opposite edge of the screen into the reflection. */
	Com_Memset( &sampler_desc, 0, sizeof( sampler_desc ) );
	sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_desc.magFilter = VK_FILTER_LINEAR;
	sampler_desc.minFilter = VK_FILTER_LINEAR;
	sampler_desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_desc.maxLod = 0.0f;
	sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	res = qvkCreateSampler( vk.device, &sampler_desc, NULL, &vk.ssr.sampler );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: sampler failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	// ---- trace set: depth, scene colour, parameters ----
	Com_Memset( bindings, 0, sizeof( bindings ) );
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	Com_Memset( &layout_desc, 0, sizeof( layout_desc ) );
	layout_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_desc.bindingCount = 3;
	layout_desc.pBindings = bindings;

	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.ssr.trace_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: trace set layout failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	// ---- composite set: just the resolved reflection ----
	layout_desc.bindingCount = 1;
	res = qvkCreateDescriptorSetLayout( vk.device, &layout_desc, NULL, &vk.ssr.composite_set_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: composite set layout failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	for ( i = 0; i < ARRAY_LEN( vk.ssr.uniform_buffer ); i++ ) {
		if ( !rt_create_host_buffer( sizeof( ssrUniform_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				&vk.ssr.uniform_buffer[i], &vk.ssr.uniform_memory[i], &vk.ssr.uniform_ptr[i] ) ) {
			ri.Printf( PRINT_WARNING, "SSR: could not create the parameter buffer - disabling\n" );
			vk_ssr_destroy();
			return;
		}
		Com_Memset( vk.ssr.uniform_ptr[i], 0, sizeof( ssrUniform_t ) );
	}

	Com_Memset( pool_sizes, 0, sizeof( pool_sizes ) );
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = 2 * NUM_COMMAND_BUFFERS + 1;  // trace: depth + scene. composite: one
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[1].descriptorCount = NUM_COMMAND_BUFFERS;

	Com_Memset( &pool_desc, 0, sizeof( pool_desc ) );
	pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_desc.maxSets = NUM_COMMAND_BUFFERS + 1;
	pool_desc.poolSizeCount = 2;
	pool_desc.pPoolSizes = pool_sizes;

	res = qvkCreateDescriptorPool( vk.device, &pool_desc, NULL, &vk.ssr.pool );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: descriptor pool failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.ssr.pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.ssr.trace_set_layout;

	for ( i = 0; i < ARRAY_LEN( vk.ssr.trace_descriptor ); i++ ) {
		res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.ssr.trace_descriptor[i] );
		if ( res < 0 ) {
			ri.Printf( PRINT_WARNING, "SSR: trace set %i failed (%s)\n", i, vk_result_string( res ) );
			vk_ssr_destroy();
			return;
		}
	}

	set_alloc.pSetLayouts = &vk.ssr.composite_set_layout;
	res = qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.ssr.composite_descriptor );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: composite set failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	/*
	Written once. Every one of these names an object that lives as long as the
	attachments do - unlike the occlusion pass, nothing here changes per map.
	*/
	for ( i = 0; i < ARRAY_LEN( vk.ssr.trace_descriptor ); i++ ) {
		VkDescriptorImageInfo image_info[2];
		VkDescriptorBufferInfo buffer_info;
		VkWriteDescriptorSet writes[3];

		Com_Memset( image_info, 0, sizeof( image_info ) );
		image_info[0].sampler = vk.rt.depth_sampler;
		image_info[0].imageView = vk.rt.depth_view;
		/*
		[QL] R19: SHADER_READ_ONLY, and not the DEPTH_STENCIL_READ_ONLY the
		occlusion pass uses. The difference is the whole bug.

		DEPTH_STENCIL_READ_ONLY_OPTIMAL exists for an image that is *still a
		depth attachment* while being sampled - which is the occlusion
		composite's situation, and why it needs that layout. The trace here
		binds depth to nothing; it only samples. In that layout a driver is free
		to leave the image in its depth-optimised form, and a fully covered tile
		in that form is metadata rather than samples - so a raw read of it comes
		back as the value the buffer was cleared to.

		Which is precisely what it looked like: flat surfaces classified as sky,
		while the geometry edges and anything that had just moved came back
		correct, because a tile holding an edge or a fresh draw cannot be
		compressed. It read as a wireframe over a red screen, it survived
		r_ext_multisample 0, and it happened to items bouncing on the spot with
		the camera perfectly still - which is what ruled out every explanation
		involving the camera.

		SHADER_READ_ONLY_OPTIMAL says the image has to be readable by a shader,
		so the transition into it is the decompression.
		*/
		image_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		image_info[1].sampler = vk.ssr.sampler;
		image_info[1].imageView = vk.color_image_view;
		image_info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		Com_Memset( &buffer_info, 0, sizeof( buffer_info ) );
		buffer_info.buffer = vk.ssr.uniform_buffer[i];
		buffer_info.range = sizeof( ssrUniform_t );

		Com_Memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = vk.ssr.trace_descriptor[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &image_info[0];

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = vk.ssr.trace_descriptor[i];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &image_info[1];

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = vk.ssr.trace_descriptor[i];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[2].pBufferInfo = &buffer_info;

		qvkUpdateDescriptorSets( vk.device, 3, writes, 0, NULL );
	}

	{
		VkDescriptorImageInfo image_info;
		VkWriteDescriptorSet write;

		Com_Memset( &image_info, 0, sizeof( image_info ) );
		image_info.sampler = vk.ssr.sampler;
		image_info.imageView = vk.ssr.image_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		Com_Memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk.ssr.composite_descriptor;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}

	Com_Memset( &pl_desc, 0, sizeof( pl_desc ) );
	pl_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_desc.setLayoutCount = 1;
	pl_desc.pSetLayouts = &vk.ssr.trace_set_layout;

	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.ssr.trace_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: trace pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	pl_desc.pSetLayouts = &vk.ssr.composite_set_layout;
	res = qvkCreatePipelineLayout( vk.device, &pl_desc, NULL, &vk.ssr.composite_pipeline_layout );
	if ( res < 0 ) {
		ri.Printf( PRINT_WARNING, "SSR: composite pipeline layout failed (%s)\n", vk_result_string( res ) );
		vk_ssr_destroy();
		return;
	}

	vk_create_post_process_pipeline( 8, glConfig.vidWidth, glConfig.vidHeight );   // trace
	vk_create_post_process_pipeline( 9, glConfig.vidWidth, glConfig.vidHeight );   // composite
	vk_create_post_process_pipeline( 10, glConfig.vidWidth, glConfig.vidHeight );  // debug composite
	/* [QL] E151: r_ssrResolution 2. Not fatal if they fail - the setting then
	   just traces at full resolution. */
	vk_create_post_process_pipeline( 13, ( glConfig.vidWidth + 1 ) / 2, ( glConfig.vidHeight + 1 ) / 2 );
	vk_create_post_process_pipeline( 14, glConfig.vidWidth, glConfig.vidHeight );
	vk_create_post_process_pipeline( 15, glConfig.vidWidth, glConfig.vidHeight );

	if ( vk.ssr.trace_pipeline == VK_NULL_HANDLE || vk.ssr.composite_pipeline == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "SSR: pipelines were not created - disabling\n" );
		vk_ssr_destroy();
		return;
	}

	vk.ssr.ready = qtrue;

	vk_ssr_create_rt();   /* [QL] E156 */
}


/*
=================
[QL] K6. vk_ssr, one job each.

vk_ssr was some 600 lines doing all of it in a row - whether to run, the
view, the water planes, the waves, the ray-traced fallback, the ripples, the
emitters, the load report, the trace and the composite. The same code, moved
as it was into one function per job, in the order it runs; vk_ssr is now the
list. No behaviour change is intended.
=================
*/
static qboolean ssr_build_view( ssrUniform_t *u )
{
	float proj[16];

	/*
	The same clip the depth buffer was rendered with, which is not
	viewParms.projectionMatrix as it stands: get_mvp_transform negates element 5
	before use, because Quake's projection is an OpenGL one and Vulkan's clip
	space has Y the other way up. Reconstructing with the unmodified matrix
	mirrors every position vertically, and the result does not look like a flip -
	it looks like the reflection sliding the wrong way as you turn. The occlusion
	pass learned this the hard way; it is the same matrix and the same fix.
	*/
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	myGlMultMatrix( backEnd.viewParms.world.modelMatrix, proj, u->viewProj );

	if ( !rt_invert_matrix( u->viewProj, u->invViewProj ) ) {
		return qfalse;
	}

	VectorCopy( backEnd.viewParms.or.origin, u->eye );
	u->eye[3] = 0.0f;

	u->params[0] = R_WaterSetting( r_ssr, tr.waterProfile.haveStrength, tr.waterProfile.strength );
	u->params[1] = r_ssrDistance->value;
	u->params[2] = (float)r_ssrSteps->integer;
	u->params[3] = r_ssrThickness->value;

	/* Same convention the occlusion pass uses - see its depthInfo. */
#ifdef USE_REVERSED_DEPTH
	u->depthInfo[0] = 0.0f;   // cleared to far
	u->depthInfo[1] = 0.6f;   // DEPTH_RANGE_WEAPON minDepth
	u->depthInfo[2] = 1.0f;   // near is 1.0
#else
	u->depthInfo[0] = 1.0f;
	u->depthInfo[1] = 0.3f;   // DEPTH_RANGE_WEAPON maxDepth
	u->depthInfo[2] = -1.0f;
#endif
	/* [QL] r_ssrDebug - see the shader. 0 to 2 are exactly representable, so the
	   equality compares over there are exact. */
	u->depthInfo[3] = (float)r_ssrDebug->integer;
	return qtrue;
}

static void ssr_build_planes( ssrUniform_t *u )
{
	int i;

	for ( i = 0; i < vk.numWaterPlanes && i < SSR_MAX_PLANES; i++ ) {
		u->planes[i][0] = vk.waterPlanes[i].normal[0];
		u->planes[i][1] = vk.waterPlanes[i].normal[1];
		u->planes[i][2] = vk.waterPlanes[i].normal[2];
		u->planes[i][3] = vk.waterPlanes[i].dist;

		VectorCopy( vk.waterPlanes[i].mins, u->boundsMin[i] );
		VectorCopy( vk.waterPlanes[i].maxs, u->boundsMax[i] );
		u->boundsMin[i][3] = u->boundsMax[i][3] = 0.0f;
	}
	u->planeCount[0] = (float)i;
	u->planeCount[1] = u->planeCount[2] = u->planeCount[3] = 0.0f;
}

static void ssr_build_waves( ssrUniform_t *u, int ssrScale )
{
	/*
	[QL] R19 waves. Both controls collapse to zero when r_waterWaves is off, so
	the shader's two early-outs cover the whole feature and there is no second
	place for it to be switched on.

	backEnd.refdef.floatTime rather than a counter of our own: it is the time
	the rest of the frame was built with, so the waves are in step with anything
	that is ever made to react to them, and it is the value a demo replays with.
	Seconds since the client started, so it is large - a float holds about a
	tenth of a millisecond of precision after a day of uptime, which is far
	finer than a wave that takes a second to cross a pool.
	*/
	if ( r_waterWaves->integer ) {
		/* [QL] R19: the map's numbers where the player has left the cvar alone,
		   theirs where they have not. See R_WaterSetting. */
		const waterProfile_t *wp = &tr.waterProfile;

		u->wave[0] = R_WaterSetting( r_waterWaveSteepness, wp->haveSteepness, wp->steepness );
		u->wave[1] = R_WaterSetting( r_waterWaveScale,     wp->haveScale,     wp->scale );
		u->wave[2] = R_WaterSetting( r_waterWaveSpeed,     wp->haveSpeed,     wp->speed );
		u->wave[3] = (float)backEnd.refdef.floatTime;
		u->wave2[0] = R_WaterSetting( r_waterWaveHeight,   wp->haveHeight,    wp->height );
	} else {
		u->wave[0] = u->wave[1] = u->wave[2] = u->wave[3] = 0.0f;
		u->wave2[0] = 0.0f;
	}

	/*
	[QL] Outside the r_waterWaves test, deliberately - it was inside it, and
	that was wrong.

	Foam is about impacts, not wind. Someone who turns the swell off to get
	still water has not asked for rockets to stop splashing, and the two
	controls reading as one made "no foam" and "waves off" the same symptom with
	no way to tell them apart. r_ssrDebug 5 showed the rings perfectly the whole
	time, because the disturbance field never depended on the wind either.
	*/
	u->wave2[1] = r_waterFoam->value;
	u->wave2[2] = (float)ssrScale;   /* [QL] E151: the trace's pixel scale */
	u->wave2[3] = 0.0f;
}

static void ssr_build_rt( ssrUniform_t *u, qboolean useRT, qboolean lightShadows )
{
	/* [QL] E156: r_ssrRayTrace - 0 whenever the ray-traced pipeline will not be
	   the one bound, so the shader's test and the binding cannot disagree */
	u->rtInfo[0] = useRT ? (float)r_ssrRayTrace->integer : 0.0f;
	u->rtInfo[1] = 8192.0f;   // past anything a pool can see across
	u->rtInfo[2] = 0.5f;      // a mid albedo: the grid gives the light, not the texture
	u->rtInfo[3] = ( useRT && lightShadows )
		? (float)( RT_SHADOW_MASK ) : 0.0f;
	if ( useRT && vk.rt.world.haveGrid && tr.world ) {
		VectorCopy( tr.world->lightGridOrigin, u->gridOrigin );
		VectorCopy( tr.world->lightGridInverseSize, u->gridInvSize );
		u->gridBounds[0] = (float)tr.world->lightGridBounds[0];
		u->gridBounds[1] = (float)tr.world->lightGridBounds[1];
		u->gridBounds[2] = (float)tr.world->lightGridBounds[2];
		u->gridOrigin[3] = 1.0f;
	} else {
		u->gridOrigin[3] = 0.0f;
	}
	u->gridInvSize[3] = u->gridBounds[3] = 0.0f;
}

static void ssr_build_ripples( ssrUniform_t *u )
{
	const int i = (int)u->planeCount[0];   /* the plane count, for the report */

	/*
	[QL] R19: the disturbances, matched to the plane each one belongs to.

	Matched here rather than in the shader because it is once per ripple per
	frame against once per ripple per pixel, and because the answer does not
	change between pixels. A ripple keeps only its world XY and an age by the
	time it reaches the shader.

	The height test is what does the matching, with a generous band: cgame calls
	this from an event, and the origin it has is a trace endpoint, a player's
	feet or a missile's last position - close to the surface but rarely exactly
	on it. An explosion a little above the water should still ripple it.

	Ripples that match nothing are dropped silently. A splash in a puddle the
	reflection pass has no plane for is not an error; there is simply nothing
	for it to disturb.
	*/
	{
		const float band = 48.0f;   // world units either side of a surface
		float now = (float)backEnd.refdef.time * 0.001f;
		int r, first, count;
		int dropAge = 0, dropPlane = 0;
		/*
		[QL] R28. Resolved once for the frame, not per ripple - these are
		global tuning, and R_WaterSetting does a string compare.

		`life` is read here as well as being handed to the shader because this
		loop is what decides a ripple is over. Leave the two on different
		numbers and a ripple either vanishes while the shader is still drawing
		it or holds a slot after it is invisible, so there is one source.
		*/
		const waterProfile_t *rwp = &tr.waterProfile;
		float rippleLife = R_WaterSetting( r_waterRippleLife, rwp->haveRippleLife, rwp->rippleLife );
		float rippleSize = R_WaterSetting( r_waterRippleSize, rwp->haveRippleSize, rwp->rippleSize );

		if ( rippleLife < 0.05f ) {
			rippleLife = 0.05f;     // it divides the age in the shader
		}

		u->rippleTune[0] = rippleLife;
		u->rippleTune[1] = R_WaterSetting( r_waterRippleWaves, rwp->haveRippleWaves, rwp->rippleWaves );
		u->rippleTune[2] = R_WaterSetting( r_waterRippleHeight, rwp->haveRippleHeight, rwp->rippleHeight );
		u->rippleTune[3] = rippleSize;

		count = 0;

		/* The ring holds at most MAX_WATER_RIPPLES; start at the oldest that
		   is still in it so the newest survive a full buffer. */
		first = tr.numWaterRipples - MAX_WATER_RIPPLES;
		if ( first < 0 ) {
			first = 0;
		}

		for ( r = first; r < tr.numWaterRipples && count < SSR_MAX_RIPPLES; r++ ) {
			waterRipple_t *rp = &tr.waterRipples[ r % MAX_WATER_RIPPLES ];
			float age;
			int p, best = -1;
			float bestDist = band;

			/* [QL] first sight of it - see RE_AddWaterRipple for why the stamp
			   happens here and not where it was added */
			if ( rp->startTime < 0 ) {
				rp->startTime = backEnd.refdef.time;
			}

			age = now - (float)rp->startTime * 0.001f;

			if ( age < 0.0f || age >= rippleLife ) {
				dropAge++;
				continue;           // not yet, or long gone
			}

			for ( p = 0; p < i; p++ ) {
				float d = fabsf( rp->origin[2] - vk.waterPlanes[p].dist );
				if ( d < bestDist ) {
					/* and within that surface's footprint, or it belongs to a
					   pool somewhere else at the same height */
					if ( rp->origin[0] >= vk.waterPlanes[p].mins[0] - band &&
					     rp->origin[0] <= vk.waterPlanes[p].maxs[0] + band &&
					     rp->origin[1] >= vk.waterPlanes[p].mins[1] - band &&
					     rp->origin[1] <= vk.waterPlanes[p].maxs[1] + band ) {
						bestDist = d;
						best = p;
					}
				}
			}

			if ( best < 0 ) {
				dropPlane++;
				continue;
			}

			u->ripple[count][0] = rp->origin[0];
			u->ripple[count][1] = rp->origin[1];
			u->ripple[count][2] = age;
			/* [QL] R28: scaled here rather than in the shader so the reach the
			   shader tests against and the reach it draws are the same number */
			u->ripple[count][3] = rp->radius * rippleSize;
			u->ripple2[count][0] = rp->strength;

			/*
			[QL] E148. Rings no wider than the water they are in.

			A rocket's ring reaches 880 units at the default size and its rings
			are about 176 apart; the flag room pools are 288 x 136, so one ring
			spanned the whole pool and a wall's echo lay on top of the wave
			that made it - the pool heaved and nothing visibly came back.

			E147 answered that by capping the reach, which was the wrong knob:
			the envelope fades to nothing at the reach, an echo has at least
			twice the distance to the wall to travel, and in the flag pool it
			arrived at about a seventh of the ring that caused it. Garden ponds
			are too big to be capped, which is why a bounce showed there and
			not in the flag room.

			So the reach - envelope, speed, where it stops - is left alone, and
			only the spacing of the rings is tightened (ripple6.x multiplies
			the wavenumber) until the ring pattern fits 1.5 times the distance
			to the far corner of the water. Strength is divided by the same
			factor, because the visible slope goes as strength times
			wavenumber; the ripple height settings mean what they did. Large
			water is unaffected.
			*/
			{
				const vkWaterPlane_t *wp = &vk.waterPlanes[ best ];
				float fx = MAX( fabsf( rp->origin[0] - wp->mins[0] ), fabsf( rp->origin[0] - wp->maxs[0] ) );
				float fy = MAX( fabsf( rp->origin[1] - wp->mins[1] ), fabsf( rp->origin[1] - wp->maxs[1] ) );
				float fit = MAX( 1.5f * sqrtf( fx * fx + fy * fy ), 32.0f );
				float tighten = MAX( 1.0f, u->ripple[count][3] / fit );

				u->ripple6[count][0] = tighten;
				u->ripple6[count][1] = u->ripple6[count][2] = u->ripple6[count][3] = 0.0f;
				u->ripple2[count][0] /= tighten;
			}

			u->ripple2[count][1] = (float)best;
			/* [QL] E142, E148: how much of it bounces off the pool's sides -
			   every splash, see RIPPLE_REFLECT_MIN's note and rippleSurface */
			u->ripple2[count][2] = r_waterRippleReflect->value;
			u->ripple2[count][3] = 0.0f;
			if ( u->ripple2[count][2] > 0.0f ) {
				vk_ripple_walls( rp->origin, u->ripple[count][3], best, u, count );
			}
			count++;
		}

		u->planeCount[1] = (float)count;

		/*
		[QL] Say why a ripple did not arrive, because "I see no ripples" has
		three causes and the console could only rule out one of them.

		RE_AddWaterRipple printing proves cgame called. r_ssrDebug 5 proves what
		reached the surface. Between those two sits this function, which drops
		ripples for exactly two reasons and used to do it in silence - and a
		silent drop is indistinguishable from a shader that is not drawing.

		Rate-limited to once a second: this runs every frame and the interesting
		case is a steady stream of drops, not any individual one.
		*/
		/*
		[QL] Say what happened to every ripple, not only the ones that were
		dropped inside the loop.

		The first version of this only printed when the loop rejected something,
		which cannot distinguish "the loop rejected them all" from "the loop
		never ran" - and the second is what a stale count, an empty buffer or a
		zero plane count all look like. It reported nothing in exactly the case
		that needed reporting.

		So: the counts, and then the newest ripple measured against the nearest
		plane, which is the pair of numbers the height match is made of. If the
		two z values are far apart the match is the fault; if they are close and
		it still dropped, the footprint test is; if the age is large the clock
		the two ends use is not the same one.
		*/
		if ( tr.numWaterRipples > 0 && backEnd.refdef.time - ssrRippleReport > 1000 ) {
			const waterRipple_t *newest =
				&tr.waterRipples[ ( tr.numWaterRipples - 1 ) % MAX_WATER_RIPPLES ];
			float bestZ = 0.0f, bestGap = 1.0e30f;
			int p;

			ssrRippleReport = backEnd.refdef.time;

			for ( p = 0; p < i; p++ ) {
				float gap = fabsf( newest->origin[2] - vk.waterPlanes[p].dist );
				if ( gap < bestGap ) {
					bestGap = gap;
					bestZ = vk.waterPlanes[p].dist;
				}
			}

			ri.Printf( PRINT_DEVELOPER,
				"SSR ripples: %i sent, %i expired, %i unmatched, %i plane(s), foam %g\n"
				"  newest at %.0f %.0f %.0f, age %.2fs, nearest plane z %.0f (gap %.0f, band %.0f)\n",
				count, dropAge, dropPlane, i, r_waterFoam->value,
				newest->origin[0], newest->origin[1], newest->origin[2],
				now - (float)newest->startTime * 0.001f,
				bestZ, bestGap, band );
		}
	}
}

static void ssr_build_emitters( ssrUniform_t *u )
{
	/*
	[QL] R19: the frame's dynamic lights, so emitters reflect.

	dl->origin and not dl->transformed, the same choice the occlusion pass
	makes and for the same reason: transformed is in the current entity's
	space, and by the time this runs there is no current entity. World space is
	what the reflected ray is in.

	Linear lights are skipped rather than approximated. A lightning beam is a
	segment, the test over in the shader is against a sphere, and a sphere at
	one end of a rail trail is worse than no reflection of it at all.
	*/
	{
		int l, count = 0, nvalid = 0, pick[MAX_DLIGHTS];
		float score[MAX_DLIGHTS];

		/*
		[QL] K3: the lights that matter most, not the first ones.

		The shader holds SSR_MAX_LIGHTS. With more in the frame, the first ones
		found used to win, so a distant torch could take the slot a rocket
		going off over the pool needed. Each light is scored by its brightness
		times its radius, discounted by how far it is from the nearest water
		box - reflections can show lights off screen, so this is not about the
		camera. Fewer than the limit: all of them, in order, as before.
		*/
		for ( l = 0; l < backEnd.refdef.num_dlights && l < MAX_DLIGHTS; l++ ) {
			const dlight_t *dl = &backEnd.refdef.dlights[l];
			float near = 1e9f, bright;
			int w;

			if ( dl->linear || dl->radius <= 0.0f ) {
				continue;
			}
			for ( w = 0; w < vk.numWaterPlanes; w++ ) {
				const vkWaterPlane_t *wp = &vk.waterPlanes[w];
				vec3_t c;
				int a;
				for ( a = 0; a < 3; a++ ) {
					c[a] = dl->origin[a] < wp->mins[a] ? wp->mins[a] : ( dl->origin[a] > wp->maxs[a] ? wp->maxs[a] : dl->origin[a] );
				}
				if ( Distance( c, dl->origin ) < near ) {
					near = Distance( c, dl->origin );
				}
			}
			bright = dl->color[0] > dl->color[1] ? dl->color[0] : dl->color[1];
			if ( dl->color[2] > bright ) bright = dl->color[2];
			score[nvalid] = bright * dl->radius * dl->radius / ( dl->radius + near );
			pick[nvalid++] = l;
		}
		if ( nvalid > SSR_MAX_LIGHTS ) {
			/* partial selection sort: the best SSR_MAX_LIGHTS to the front,
			   then back into frame order so the list is stable */
			int a, b;
			for ( a = 0; a < SSR_MAX_LIGHTS; a++ ) {
				int best = a;
				for ( b = a + 1; b < nvalid; b++ ) {
					if ( score[b] > score[best] ) best = b;
				}
				if ( best != a ) {
					float ts = score[a]; int tp = pick[a];
					score[a] = score[best]; pick[a] = pick[best];
					score[best] = ts; pick[best] = tp;
				}
			}
			for ( a = 1; a < SSR_MAX_LIGHTS; a++ ) {
				for ( b = a; b > 0 && pick[b - 1] > pick[b]; b-- ) {
					int tp = pick[b]; pick[b] = pick[b - 1]; pick[b - 1] = tp;
				}
			}
			nvalid = SSR_MAX_LIGHTS;
		}

		for ( l = 0; l < nvalid; l++ ) {
			const dlight_t *dl = &backEnd.refdef.dlights[ pick[l] ];

			u->emitter[count][0] = dl->origin[0];
			u->emitter[count][1] = dl->origin[1];
			u->emitter[count][2] = dl->origin[2];
			u->emitter[count][3] = dl->radius;

			u->emitter2[count][0] = dl->color[0];
			u->emitter2[count][1] = dl->color[1];
			u->emitter2[count][2] = dl->color[2];
			u->emitter2[count][3] = r_ssrEmitters->value;
			count++;
		}

		u->planeCount[2] = (float)count;
	}
}

static void ssr_report( const ssrUniform_t *u )
{
	if ( !ssrReported ) {
		ssrReported = qtrue;
		ri.Printf( PRINT_ALL, "SSR: reflecting in %i water plane(s) - strength %g, "
			"%g units over %i steps\n",
			vk.numWaterPlanes, r_ssr->value, r_ssrDistance->value, r_ssrSteps->integer );
		/*
		[QL] What the pass was actually built with, rather than what the source
		says it was built with.

		Both of these have been guessed at across several rounds and both were
		guessed wrong. The depth numbers say which convention this build
		compiled - they decide what counts as sky and what counts as the view
		weapon, and a reflection appearing on the gun is exactly what a wrong
		weapon band looks like. The blend says whether the reflection replaces
		what is under it or adds to it, which is the difference between a debug
		view that means something and one that shows the scene tinted.

		Two lines in the log, and neither question ever has to be argued from
		the source again.
		*/
		ri.Printf( PRINT_ALL, "SSR: depth cleared to %g, weapon band from %g, near is %s\n",
			u->depthInfo[0], u->depthInfo[1], u->depthInfo[2] > 0.0f ? "1.0" : "0.0" );
		ri.Printf( PRINT_ALL, "SSR: composite blends src_alpha/one_minus_src_alpha%s\n",
			vk.ssr.debug_pipeline != VK_NULL_HANDLE
				? "; debug views go on unblended" : "; no unblended debug pipeline" );
	}
}

static void ssr_trace( qboolean useRT, int ssrScale )
{
	vk_timing_begin( RTT_SSR );   /* [QL] E177 */
	vk_end_render_pass();   // end main

	/*
	[QL] E156: the rays trace this frame's top level structure, and only the
	occlusion pass builds it - with r_rtao off nobody had, and every ray came
	back empty against a structure that was never filled. Built here instead
	when the occlusion pass did not, in the same gap outside a render pass.
	*/
	if ( useRT && !backEnd.doneRTDynamic ) {
		vk_rt_build_dynamic_tlas();
	}

	/*
	[QL] R19: depth becomes a texture here, and the layout it becomes is the
	whole of this bug.

	The trace samples depth outside any render pass, so nothing transitions the
	image implicitly - the barrier is also the synchronisation, standing in for
	the subpass dependency that did the job while this shared the main
	framebuffer. That much this had from the start, copied from the occlusion
	pass, including its choice of DEPTH_STENCIL_READ_ONLY_OPTIMAL. Copying that
	last part was the mistake, and see the trace descriptor for why: that layout
	is for an image still bound as a depth attachment while being sampled, and
	it lets a driver keep the image compressed. The trace binds depth to
	nothing. It wants SHADER_READ_ONLY_OPTIMAL, which is the layout that
	guarantees a shader can read what is in there.

	The symptom was a depth buffer that read back as the cleared value over
	every flat surface while geometry edges and freshly moved objects read
	correctly - compressed tiles against tiles that cannot be compressed. It
	looked like a wireframe over a red screen, which is not a shape that
	suggests a layout, and four rounds went into the mask before r_ssrDebug 3
	and an item bouncing on the spot with the camera still made it obvious.

	Unconditional, and it has to be: the occlusion pass puts depth back to
	ATTACHMENT_OPTIMAL when its composite ends, so the layout here is the same
	whether or not r_rtao ran.
	*/
	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		glConfig.stencilBits ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		0, 0 );

	/* ---- pass 1: march, into the offscreen target ---- */
	/* [QL] E151: r_ssrResolution 2 marches a half-size area - the render
	   area below and the half-size pipeline agree on it */
	vk.renderWidth = ( glConfig.vidWidth + ssrScale - 1 ) / ssrScale;
	vk.renderHeight = ( glConfig.vidHeight + ssrScale - 1 ) / ssrScale;
	vk.renderScaleX = vk.renderScaleY = 1.0f;
	/*
	[QL] Begun by hand rather than through vk_begin_render_pass, because that
	helper's clear path is written for the main framebuffer - two or three
	attachments, with a depth clear in the middle. This pass has one colour
	attachment and wants it transparent black. See the pass description for why
	it is cleared at all.
	*/
	{
		VkRenderPassBeginInfo rp;
		VkClearValue clear;

		Com_Memset( &clear, 0, sizeof( clear ) );

		rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rp.pNext = NULL;
		rp.renderPass = vk.ssr.offscreen_pass;
		rp.framebuffer = vk.ssr.framebuffer;
		rp.renderArea.offset.x = 0;
		rp.renderArea.offset.y = 0;
		rp.renderArea.extent.width = vk.renderWidth;
		rp.renderArea.extent.height = vk.renderHeight;
		rp.clearValueCount = 1;
		rp.pClearValues = &clear;

		qvkCmdBeginRenderPass( vk.cmd->command_buffer, &rp, VK_SUBPASS_CONTENTS_INLINE );

		vk.cmd->last_pipeline = VK_NULL_HANDLE;
		vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	}

	/* [QL] E156: say which trace is running whenever that changes - a toggle
	   that silently did nothing is the failure this has to rule out */
	{
		static int lastRT = -1;
		const int nowRT = useRT ? 1 : 0;
		if ( nowRT != lastRT ) {
			lastRT = nowRT;
			ri.Printf( PRINT_ALL, "SSR: %s (r_ssrRayTrace %i, fallback %s)\n",
				useRT ? "march + ray-traced fallback" : "march only",
				r_ssrRayTrace->integer, vk.ssr.rtReady ? "ready" : "not available" );
		}
	}
	if ( useRT ) {   /* [QL] E156 */
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			ssrScale > 1 ? vk.ssr.rt_trace_pipeline_half : vk.ssr.rt_trace_pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.ssr.rt_pipeline_layout, 0, 1, &vk.ssr.rt_descriptor[ vk.cmd_index ], 0, NULL );
	} else {
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			ssrScale > 1 ? vk.ssr.trace_pipeline_half : vk.ssr.trace_pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.ssr.trace_pipeline_layout, 0, 1, &vk.ssr.trace_descriptor[ vk.cmd_index ], 0, NULL );
	}
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_render_pass();

	/*
	[QL] And hand it back in the layout the composite pass expects.

	The composite borrows the occlusion pass, whose attachment description says
	depth arrives as DEPTH_STENCIL_READ_ONLY_OPTIMAL and leaves as an
	attachment - so the pair still puts the image exactly where the 2D and bloom
	passes that inherit it want it. This one barrier is what keeps the trace
	free to ask for a layout of its own without the composite having to know.
	*/
	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		glConfig.stencilBits ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		0, 0 );
}

static void ssr_composite( int ssrScale )
{
	/* ---- pass 2: blend it over the scene ---- */
	vk_begin_rtao_render_pass();

	/* [QL] A debug view goes on unblended - see vk.ssr.debug_pipeline. Falls
	   back to the blending one if that pipeline was not created, which is worth
	   nothing but is better than binding a null handle. */
	if ( ssrScale > 1 ) {   /* [QL] E151: the ones that read the half-size trace */
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			( r_ssrDebug->integer != 0 && vk.ssr.debug_pipeline_half != VK_NULL_HANDLE )
				? vk.ssr.debug_pipeline_half : vk.ssr.composite_pipeline_half );
	} else {
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			( r_ssrDebug->integer != 0 && vk.ssr.debug_pipeline != VK_NULL_HANDLE )
				? vk.ssr.debug_pipeline : vk.ssr.composite_pipeline );
	}
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.ssr.composite_pipeline_layout, 0, 1, &vk.ssr.composite_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_composite_render_pass();   // [QL] E150: depth writable for the rest of the frame
	vk_timing_end( RTT_SSR );   /* [QL] E177 */

	/*
	Put back what these passes clobbered - the same repair the occlusion pass
	makes, for the same reason. Binding a descriptor set with a different
	pipeline layout invalidates whatever the geometry path had bound at those
	indices, and the renderer only rebinds a descriptor when its value changes.
	Without this the 2D pass that follows draws against bindings that no longer
	exist, which is what ate the HUD once already.
	*/
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	vk_update_mvp( NULL );
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
}

qboolean vk_ssr( void )
{
	ssrUniform_t *u;
	/* [QL] E151: r_ssrResolution, and only if every half-size pipeline exists */
	const int ssrScale = ( r_ssrResolution && r_ssrResolution->integer >= 2 &&
		vk.ssr.trace_pipeline_half != VK_NULL_HANDLE && vk.ssr.composite_pipeline_half != VK_NULL_HANDLE &&
		vk.ssr.debug_pipeline_half != VK_NULL_HANDLE ) ? 2 : 1;
	/* [QL] E156: r_ssrRayTrace, and only with this map's sets written and the
	   pipeline for the chosen resolution built */
	/* [QL] E165: also when the lights cast ray-traced shadows, for the emitter
	   visibility test - rtInfo.x still says whether the fallback itself runs */
	const qboolean lightShadows = ( r_rtDlightShadows->integer || R_SHADOWS_TRACED ) ? qtrue : qfalse;
	const qboolean useRT = ( r_ssrRayTrace && ( r_ssrRayTrace->integer > 0 || lightShadows ) && vk.ssr.rtReady &&
		( ssrScale > 1 ? vk.ssr.rt_trace_pipeline_half : vk.ssr.rt_trace_pipeline ) != VK_NULL_HANDLE );

	if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP ) {
		return qfalse;
	}
	if ( backEnd.doneSSR || !backEnd.doneSurfaces ) {
		return qfalse;   // already run this frame, or there is no 3D yet
	}
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return qfalse;   // [QL] E175: a UI model view - see vk_actor_shadows
	}
	if ( r_ssr == NULL || r_ssr->value <= 0.0f ) {
		return qfalse;
	}
	if ( !vk.ssr.ready ) {
		if ( !ssrReported ) {
			ssrReported = qtrue;
			ri.Printf( PRINT_ALL, "SSR: not running - the pass was not created\n" );
		}
		return qfalse;
	}
	if ( vk.numWaterPlanes == 0 ) {
		if ( !ssrReported ) {
			ssrReported = qtrue;
			ri.Printf( PRINT_ALL, "SSR: not running - this map has no water plane to reflect in\n" );
		}
		return qfalse;
	}

	u = (ssrUniform_t *)vk.ssr.uniform_ptr[ vk.cmd_index ];
	if ( u == NULL ) {
		return qfalse;
	}

	if ( !ssr_build_view( u ) ) {
		return qfalse;
	}
	ssr_build_planes( u );
	ssr_build_waves( u, ssrScale );
	ssr_build_rt( u, useRT, lightShadows );
	ssr_build_ripples( u );
	ssr_build_emitters( u );
	ssr_report( u );

	ssr_trace( useRT, ssrScale );
	ssr_composite( ssrScale );

	backEnd.doneSSR = qtrue;

	return qtrue;
}
