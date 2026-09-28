/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"


/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

/*
[QL] E160: silhouette edges by signed count, not by pairing.

Quake 3 kept up to 32 edges per vertex (MAX_EDGE_DEFS) and called an edge a
silhouette when no light-facing triangle ran along it the other way. Two ways
that goes wrong, both common in Quake Live's denser models:

  - a vertex shared by more than 32 triangles (the centre of a fan, a
    tightly packed barrel) silently dropped the rest, so an interior edge
    lost its partner and was extruded as a silhouette
  - an edge shared by three or more triangles ("overfanned", as the old
    comment called it) extruded as many sides as it had unpaired uses

Each such stray side is a single blade, not a closed volume. Depth-pass (Quake
3's counting) only saw it in front of the visible surface, where it is rarely
anything but a sliver; depth-fail (E155) counts what is behind the surface
too, so a blade under the floor became a black line across it - the floating
shotgun's line through the floor.

Instead: every light-facing triangle adds +1 to each of its edges walked one
way and -1 walked the other, in a hash on the vertex pair. Interior edges sum
to zero whatever the mesh looks like; what is left over is the silhouette,
with the sign giving its direction and the magnitude how many sides it needs.
No per-vertex limit, and the volume closes on any mesh.
*/
#define EDGE_HASH_SIZE	( 1 << 16 )	/* power of two, > 2x the most edges SHADER_MAX_INDEXES allows */

typedef struct {
	int		lo, hi;		// vertex pair, lo < hi
	int		count;		// +1 per facing triangle walking lo->hi, -1 per hi->lo
	int		stamp;		// == edgeStamp when this slot is in use this pass
} shadowEdge_t;

static	shadowEdge_t	edgeHash[EDGE_HASH_SIZE];
static	int				edgeUsed[SHADER_MAX_INDEXES];	// slots in use, in insertion order
static	int				numEdgesUsed;
static	int				edgeStamp;
/* [QL] E155: the light-facing triangles, kept for the caps - R_CalcShadowEdges
   overwrites tess.indexes with the volume */
static	int			capTris[SHADER_MAX_INDEXES/3][3];
static	int			numCapTris;

static void R_AddShadowEdge( int a, int b ) {
	const int lo = a < b ? a : b;
	const int hi = a < b ? b : a;
	unsigned int h = ( (unsigned int)lo * 73856093u ) ^ ( (unsigned int)hi * 19349663u );
	shadowEdge_t *e;

	if ( a == b ) {
		return;		// a degenerate triangle's edge has no side to extrude
	}
	h &= EDGE_HASH_SIZE - 1;
	for ( ;; ) {
		e = &edgeHash[ h ];
		if ( e->stamp != edgeStamp ) {
			if ( numEdgesUsed >= ARRAY_LEN( edgeUsed ) ) {
				return;		// cannot happen with EDGE_HASH_SIZE as set; never loop forever
			}
			e->stamp = edgeStamp;
			e->lo = lo;
			e->hi = hi;
			e->count = 0;
			edgeUsed[ numEdgesUsed++ ] = (int)h;
			break;
		}
		if ( e->lo == lo && e->hi == hi ) {
			break;
		}
		h = ( h + 1 ) & ( EDGE_HASH_SIZE - 1 );
	}
	e->count += ( a < b ) ? 1 : -1;
}


static void R_CalcShadowEdges( void ) {
	int		i;
	int		c;
	int		j;
	int		i2;
	color4ub_t *colors;

	tess.numIndexes = 0;

	// what is left of each edge after the light-facing triangles on both
	// sides cancel is the silhouette - see R_AddShadowEdge
	for ( j = 0; j < numEdgesUsed; j++ ) {
		const shadowEdge_t *e = &edgeHash[ edgeUsed[ j ] ];
		const int dir = e->count > 0 ? 1 : -1;

		/* the directed edge as a facing triangle walks it */
		i = ( dir > 0 ) ? e->lo : e->hi;
		i2 = ( dir > 0 ) ? e->hi : e->lo;

		for ( c = e->count * dir; c > 0; c-- ) {
			if ( tess.numIndexes > ARRAY_LEN( tess.indexes ) - 6 ) {
				j = numEdgesUsed;
				break;
			}
#ifdef USE_VULKAN
			tess.indexes[ tess.numIndexes + 0 ] = i;
			tess.indexes[ tess.numIndexes + 1 ] = i2;
			tess.indexes[ tess.numIndexes + 2 ] = i + tess.numVertexes;
			tess.indexes[ tess.numIndexes + 3 ] = i2;
			tess.indexes[ tess.numIndexes + 4 ] = i2 + tess.numVertexes;
			tess.indexes[ tess.numIndexes + 5 ] = i + tess.numVertexes;
#else
			tess.indexes[ tess.numIndexes + 0 ] = i;
			tess.indexes[ tess.numIndexes + 1 ] = i + tess.numVertexes;
			tess.indexes[ tess.numIndexes + 2 ] = i2;
			tess.indexes[ tess.numIndexes + 3 ] = i2;
			tess.indexes[ tess.numIndexes + 4 ] = i + tess.numVertexes;
			tess.indexes[ tess.numIndexes + 5 ] = i2 + tess.numVertexes;
#endif
			tess.numIndexes += 6;
		}
	}

#ifdef USE_VULKAN
	/*
	[QL] E155. Close the volume - depth-fail counting needs it closed.

	The sides above are the silhouette pushed away from the light. Every
	light-facing triangle then caps it twice: where it is (the near cap) and
	where the push took it (the far cap). Windings chosen to face out of the
	volume in the same sense the sides do: the sides here are (a, b, a'),
	the Vulkan reverse of the GL build's order, so the near cap is the facing
	triangle reversed, (a, c, b), and the far cap is (a', b', c').
	*/
	/* [QL] E159: only for depth-fail (see SHADOW_EDGES). Depth-pass does not
	   need caps, and the near one would z-fight with the model. */
	for ( i = 0; i < numCapTris && vk.depthClamp; i++ ) {
		const int a = capTris[i][0], b = capTris[i][1], c = capTris[i][2];

		if ( tess.numIndexes > ARRAY_LEN( tess.indexes ) - 6 ) {
			break;   // an incomplete cap leaves a leak in one shadow, not a crash
		}
		tess.indexes[ tess.numIndexes + 0 ] = a;
		tess.indexes[ tess.numIndexes + 1 ] = c;
		tess.indexes[ tess.numIndexes + 2 ] = b;
		tess.indexes[ tess.numIndexes + 3 ] = a + tess.numVertexes;
		tess.indexes[ tess.numIndexes + 4 ] = b + tess.numVertexes;
		tess.indexes[ tess.numIndexes + 5 ] = c + tess.numVertexes;
		tess.numIndexes += 6;
	}

	tess.numVertexes *= 2;

	colors = &tess.svars.colors[0][0]; // we need at least 2x SHADER_MAX_VERTEXES there

	for ( i = 0; i < tess.numVertexes; i++ ) {
		Vector4Set( colors[i].rgba, 50, 50, 50, 255 );
	}
#endif
}


/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_ShadowTessEnd( void ) {
	int		i;
	int		numTris;
	vec3_t	lightDir;
#ifdef USE_VULKAN
	uint32_t pipeline[2];
#else
	GLboolean rgba[4];
#endif

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 2 && r_shadows->integer == 2 )
		VectorCopy( backEnd.currentEntity->shadowLightDir, lightDir );
	else
#endif
		VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	// clamp projection by height
	if ( lightDir[2] > 0.1 ) {
		float s = 0.1 / lightDir[2];
		VectorScale( lightDir, s, lightDir );
	}

	// project vertexes away from light direction
	for ( i = 0; i < tess.numVertexes; i++ ) {
		VectorMA( tess.xyz[i], -512, lightDir, tess.xyz[i+tess.numVertexes] );
	}

#ifdef USE_VULKAN
	/*
	[QL] E159: lift the near end of the volume one unit off the model.

	The near cap is the model's own triangles, so it sits at exactly the depth
	the model wrote. Depth-fail counts it when it fails the depth test - and at
	equal depth whether it does is down to rounding, so the model's side away
	from the light speckled in and out of its own shadow as the light angle
	changed ("self-player under the right lighting"). One unit away from the
	light puts the cap in front of that surface, where it always passes. After
	the far vertices are made, so the volume's length is untouched.
	*/
	if ( vk.depthClamp ) {
		vec3_t away;
		VectorCopy( lightDir, away );
		if ( VectorNormalize( away ) > 0.0f ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				VectorMA( tess.xyz[i], -1.0f, away, tess.xyz[i] );
			}
		}
	}
#endif

	// decide which triangles face the light
	edgeStamp++;          // [QL] E160: empties the edge hash without clearing it
	if ( edgeStamp == 0 ) {   // wrapped: stale stamps could now match
		Com_Memset( edgeHash, 0, sizeof( edgeHash ) );
		edgeStamp = 1;
	}
	numEdgesUsed = 0;
	numCapTris = 0;   // [QL] E155

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;
		vec3_t	d1, d2, normal;
		float	*v1, *v2, *v3;
		float	d;

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		v1 = tess.xyz[ i1 ];
		v2 = tess.xyz[ i2 ];
		v3 = tess.xyz[ i3 ];

		VectorSubtract( v2, v1, d1 );
		VectorSubtract( v3, v1, d2 );
		CrossProduct( d1, d2, normal );

		d = DotProduct( normal, lightDir );
		if ( d <= 0 ) {
			continue;   // [QL] E160: only light-facing triangles make edges or caps
		}

		{   // [QL] E155: a cap triangle
			capTris[ numCapTris ][0] = i1;
			capTris[ numCapTris ][1] = i2;
			capTris[ numCapTris ][2] = i3;
			numCapTris++;
		}

		// create the edges
		R_AddShadowEdge( i1, i2 );
		R_AddShadowEdge( i2, i3 );
		R_AddShadowEdge( i3, i1 );
	}

	R_CalcShadowEdges();

	// draw the silhouette edges
#ifdef USE_VULKAN
	GL_Bind( tr.whiteImage );

	// mirrors have the culling order reversed
	if ( backEnd.viewParms.portalView == PV_MIRROR ) {
		pipeline[0] = vk.shadow_volume_pipelines[0][1];
		pipeline[1] = vk.shadow_volume_pipelines[1][1];
	} else {
		pipeline[0] = vk.shadow_volume_pipelines[0][0];
		pipeline[1] = vk.shadow_volume_pipelines[1][0];

	}
	vk_bind_pipeline( pipeline[0] ); // back-sided
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
	vk_bind_pipeline( pipeline[1] ); // front-sided
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	tess.numVertexes /= 2;
#else
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );

	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	if ( qglLockArraysEXT )
		qglLockArraysEXT( 0, tess.numVertexes*2 );

	// draw the silhouette edges

	qglDisable( GL_TEXTURE_2D );
	//GL_Bind( tr.whiteImage );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor4f( 0.2f, 0.2f, 0.2f, 1.0f );

	// don't write to the color buffer
	qglGetBooleanv( GL_COLOR_WRITEMASK, rgba );
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	GL_Cull( CT_BACK_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

	R_DrawElements( tess.numIndexes, tess.indexes );

	GL_Cull( CT_FRONT_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

	R_DrawElements( tess.numIndexes, tess.indexes );

	if ( qglUnlockArraysEXT )
		qglUnlockArraysEXT();

	// re-enable writing to the color buffer
	qglColorMask(rgba[0], rgba[1], rgba[2], rgba[3]);

	qglEnable( GL_TEXTURE_2D );
#endif

	backEnd.doneShadows = qtrue;

	tess.numIndexes = 0;
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( void ) {
#ifdef USE_VULKAN
	float tmp[16];
	int i;
#endif
	static const vec3_t verts[4] = {
		{ -100, 100, -10 },
		{  100, 100, -10 },
		{ -100,-100, -10 },
		{  100,-100, -10 }
	};

	if ( !backEnd.doneShadows ) {
		return;
	}

	backEnd.doneShadows = qfalse;

	if ( r_shadows->integer != 2 ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef USE_VULKAN
	GL_Bind( tr.whiteImage );

	for ( i = 0; i < 4; i++ )
	{
		VectorCopy( verts[i], tess.xyz[i] );
		Vector4Set( tess.svars.colors[0][i].rgba, 153, 153, 153, 255 );
	}

	tess.numVertexes = 4;

	Com_Memcpy( tmp, vk_world.modelview_transform, 64 );
	Com_Memset( vk_world.modelview_transform, 0, 64 );

	vk_world.modelview_transform[0] = 1.0f;
	vk_world.modelview_transform[5] = 1.0f;
	vk_world.modelview_transform[10] = 1.0f;
	vk_world.modelview_transform[15] = 1.0f;

	vk_bind_pipeline( vk.shadow_finish_pipeline );

	vk_update_mvp( NULL );

	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 /*| TESS_ST0 */ );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qfalse );

	Com_Memcpy( vk_world.modelview_transform, tmp, 64 );

	tess.numIndexes = 0;
	tess.numVertexes = 0;

#else
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );

	qglDisable( GL_CLIP_PLANE0 );
	GL_Cull( CT_TWO_SIDED );

	qglDisable( GL_TEXTURE_2D );

	qglLoadIdentity();

	qglColor4f( 0.6f, 0.6f, 0.6f, 1 );
	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );

	//qglColor4f( 1, 0, 0, 1 );
	//GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, 0, verts );
	qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	qglColor4f( 1, 1, 1, 1 );
	qglDisable( GL_STENCIL_TEST );

	qglEnable( GL_TEXTURE_2D );
#endif
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	float	*xyz;
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	xyz = ( float * ) tess.xyz;

	ground[0] = backEnd.or.axis[0][2];
	ground[1] = backEnd.or.axis[1][2];
	ground[2] = backEnd.or.axis[2][2];

	groundDist = backEnd.or.origin[2] - backEnd.currentEntity->e.shadowPlane;

#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 2 && r_shadows->integer == 2 )
		VectorCopy( backEnd.currentEntity->shadowLightDir, lightDir );
	else
#endif
		VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5 ) {
		VectorMA( lightDir, (0.5 - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0 / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++, xyz += 4 ) {
		h = DotProduct( xyz, ground ) + groundDist;

		xyz[0] -= light[0] * h;
		xyz[1] -= light[1] * h;
		xyz[2] -= light[2] * h;
	}
}
