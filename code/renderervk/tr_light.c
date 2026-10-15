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
// tr_light.c

#include "tr_local.h"

#define	DLIGHT_AT_RADIUS		16
// at the edge of a dlight's influence, this amount of light will be added

#define	DLIGHT_MINIMUM_RADIUS	16
// never calculate a range less than this to prevent huge light numbers


/*
===============
R_TransformDlights

Transforms the origins of an array of dlights.
Used by both the front end (for DlightBmodel) and
the back end (before doing the lighting calculation)
===============
*/
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *or) {
	int		i;
	vec3_t	temp, temp2;

	for ( i = 0 ; i < count ; i++, dl++ ) {
		VectorSubtract( dl->origin, or->origin, temp );
		dl->transformed[0] = DotProduct( temp, or->axis[0] );
		dl->transformed[1] = DotProduct( temp, or->axis[1] );
		dl->transformed[2] = DotProduct( temp, or->axis[2] );
		if ( dl->linear ) {
			VectorSubtract( dl->origin2, or->origin, temp2 );
			dl->transformed2[0] = DotProduct( temp2, or->axis[0] );
			dl->transformed2[1] = DotProduct( temp2, or->axis[1] );
			dl->transformed2[2] = DotProduct( temp2, or->axis[2] );
		}
	}
}


#ifdef USE_LEGACY_DLIGHTS
/*
=============
R_DlightBmodel

Determine which dynamic lights may effect this bmodel
=============
*/
void R_DlightBmodel( bmodel_t *bmodel ) {
	int			i, j;
	const dlight_t	*dl;
	int			mask;
	msurface_t	*surf;

	// transform all the lights
	R_TransformDlights( tr.refdef.num_dlights, tr.refdef.dlights, &tr.or );

	mask = 0;
	for ( i = 0; i < tr.refdef.num_dlights; i++ ) {
		dl = &tr.refdef.dlights[i];

		// see if the point is close enough to the bounds to matter
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( dl->transformed[j] - bmodel->bounds[1][j] > dl->radius ) {
				break;
			}
			if ( bmodel->bounds[0][j] - dl->transformed[j] > dl->radius ) {
				break;
			}
		}
		if ( j < 3 ) {
			continue;
		}

		// we need to check this light
		mask |= 1 << i;
	}

	tr.currentEntity->needDlights = (mask != 0) ? 1 : 0;

	// set the dlight bits in all the surfaces
	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		surf = bmodel->firstSurface + i;

		if ( *surf->data == SF_FACE ) {
			((srfSurfaceFace_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_GRID ) {
			((srfGridMesh_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_TRIANGLES ) {
			((srfTriangles_t *)surf->data)->dlightBits = mask;
		}
	}
}
#endif // USE_LEGACY_DLIGHTS


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

extern	cvar_t	*r_ambientScale;
extern	cvar_t	*r_directedScale;
extern	cvar_t	*r_debugLight;

/*
=================
R_SetupEntityLightingGrid

=================
*/
static void R_SetupEntityLightingGrid( trRefEntity_t *ent ) {
	vec3_t	lightOrigin;
	int		pos[3];
	int		i, j;
	byte	*gridData;
	float	frac[3];
	int		gridStep[3];
	vec3_t	direction;
	float	totalFactor;

	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// separate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	VectorSubtract( lightOrigin, tr.world->lightGridOrigin, lightOrigin );
	for ( i = 0 ; i < 3 ; i++ ) {
		float	v;

		v = lightOrigin[i]*tr.world->lightGridInverseSize[i];
		pos[i] = floor( v );
		frac[i] = v - pos[i];
		if ( pos[i] < 0 ) {
			pos[i] = 0;
		} else if ( pos[i] > tr.world->lightGridBounds[i] - 1 ) {
			pos[i] = tr.world->lightGridBounds[i] - 1;
		}
	}

	VectorClear( ent->ambientLight );
	VectorClear( ent->directedLight );
	VectorClear( direction );

	assert( tr.world->lightGridData ); // NULL with -nolight maps

	// trilerp the light value
	gridStep[0] = 8;
	gridStep[1] = 8 * tr.world->lightGridBounds[0];
	gridStep[2] = 8 * tr.world->lightGridBounds[0] * tr.world->lightGridBounds[1];
	gridData = tr.world->lightGridData + pos[0] * gridStep[0]
		+ pos[1] * gridStep[1] + pos[2] * gridStep[2];

	totalFactor = 0;
	for ( i = 0 ; i < 8 ; i++ ) {
		float	factor;
		byte	*data;
		int		lat, lng;
		vec3_t	normal;
		factor = 1.0;
		data = gridData;
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( i & (1<<j) ) {
				if ( pos[j] + 1 > tr.world->lightGridBounds[j] - 1 ) {
					break; // ignore values outside lightgrid
				}
				factor *= frac[j];
				data += gridStep[j];
			} else {
				factor *= (1.0f - frac[j]);
			}
		}

		if ( j != 3 ) {
			continue;
		}

		if ( !(data[0]+data[1]+data[2]) ) {
			continue;	// ignore samples in walls
		}
		totalFactor += factor;

		ent->ambientLight[0] += factor * data[0];
		ent->ambientLight[1] += factor * data[1];
		ent->ambientLight[2] += factor * data[2];

		ent->directedLight[0] += factor * data[3];
		ent->directedLight[1] += factor * data[4];
		ent->directedLight[2] += factor * data[5];

		lat = data[7];
		lng = data[6];
		lat *= (FUNCTABLE_SIZE/256);
		lng *= (FUNCTABLE_SIZE/256);

		// decode X as cos( lat ) * sin( long )
		// decode Y as sin( lat ) * sin( long )
		// decode Z as cos( long )

		normal[0] = tr.sinTable[(lat+(FUNCTABLE_SIZE/4))&FUNCTABLE_MASK] * tr.sinTable[lng];
		normal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		normal[2] = tr.sinTable[(lng+(FUNCTABLE_SIZE/4))&FUNCTABLE_MASK];

		VectorMA( direction, factor, normal, direction );
	}

	if ( totalFactor > 0 && totalFactor < 0.99 ) {
		totalFactor = 1.0f / totalFactor;
		VectorScale( ent->ambientLight, totalFactor, ent->ambientLight );
		VectorScale( ent->directedLight, totalFactor, ent->directedLight );
	}

	VectorScale( ent->ambientLight, r_ambientScale->value, ent->ambientLight );
	VectorScale( ent->directedLight, r_directedScale->value, ent->directedLight );

	VectorNormalize2( direction, ent->lightDir );
}


/*
===============
LogLight
===============
*/
static void LogLight( const trRefEntity_t *ent ) {
	int	max1, max2;

	if ( !(ent->e.renderfx & RF_FIRST_PERSON ) ) {
		return;
	}

	max1 = ent->ambientLight[0];
	if ( ent->ambientLight[1] > max1 ) {
		max1 = ent->ambientLight[1];
	} else if ( ent->ambientLight[2] > max1 ) {
		max1 = ent->ambientLight[2];
	}

	max2 = ent->directedLight[0];
	if ( ent->directedLight[1] > max2 ) {
		max2 = ent->directedLight[1];
	} else if ( ent->directedLight[2] > max2 ) {
		max2 = ent->directedLight[2];
	}

	ri.Printf( PRINT_ALL, "amb:%i  dir:%i\n", max1, max2 );
}


/*
=================
R_SetupEntityLighting

Calculates all the lighting values that will be used
by the Calc_* functions
=================
*/
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent ) {
	int				i;
	const dlight_t		*dl;
	float			power;
	vec3_t			dir;
	float			d;
	vec3_t			lightDir;
	vec3_t			lightOrigin;
#ifdef USE_PMLIGHT
	vec3_t			shadowLightDir;
#endif

	// lighting calculations
	if ( ent->lightingCalculated ) {
		return;
	}
	ent->lightingCalculated = qtrue;

	//
	// trace a sample point down to find ambient light
	//
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// separate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	// if NOWORLDMODEL, only use dynamic lights (menu system, etc)
	if ( !(refdef->rdflags & RDF_NOWORLDMODEL )
		&& tr.world->lightGridData ) {
		R_SetupEntityLightingGrid( ent );
	} else {
		ent->ambientLight[0] = ent->ambientLight[1] =
			ent->ambientLight[2] = tr.identityLight * 150;
		ent->directedLight[0] = ent->directedLight[1] =
			ent->directedLight[2] = tr.identityLight * 150;
		VectorCopy( tr.sunDirection, ent->lightDir );
	}

	// bonus items and view weapons have a fixed minimum add
	if ( 1 /* ent->e.renderfx & RF_MINLIGHT */ ) {
		// give everything a minimum light add
		ent->ambientLight[0] += tr.identityLight * 32;
		ent->ambientLight[1] += tr.identityLight * 32;
		ent->ambientLight[2] += tr.identityLight * 32;
	}

	//
	// modify the light by dynamic lights
	//
	d = VectorLength( ent->directedLight );
	VectorScale( ent->lightDir, d, lightDir );
#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 2 ) {
		// only direct lights
		// but we need to deal with shadow light direction
		VectorCopy( lightDir, shadowLightDir );
		if ( R_STENCIL_SHADOWS ) {
			for ( i = 0 ; i < refdef->num_dlights ; i++ ) {
				dl = &refdef->dlights[i];
				if ( dl->linear ) // no support for linear lights atm
					continue;
				VectorSubtract( dl->origin, lightOrigin, dir );
				d = VectorNormalize( dir );
				power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
				if ( d < DLIGHT_MINIMUM_RADIUS ) {
					d = DLIGHT_MINIMUM_RADIUS;
				}
				d = power / ( d * d );
				VectorMA( shadowLightDir, d, dir, shadowLightDir );
			}
		} // if ( R_STENCIL_SHADOWS )
	}  // if ( r_dlightMode->integer == 2 )
	else
#endif
	for ( i = 0 ; i < refdef->num_dlights ; i++ ) {
		dl = &refdef->dlights[i];
		VectorSubtract( dl->origin, lightOrigin, dir );
		d = VectorNormalize( dir );

		power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
		if ( d < DLIGHT_MINIMUM_RADIUS ) {
			d = DLIGHT_MINIMUM_RADIUS;
		}
		d = power / ( d * d );

		/*
		[QL] The dynamic light contribution belongs in the same space as
		everything else in this function.

		Every other term here is built in identityLight space - the no-lightgrid
		fallback is identityLight * 150, the minimum add is identityLight * 32,
		and ambient is clamped to identityLightByte. The light grid is shifted
		to match at load. This one line was in absolute 0..255, which is only
		the same thing when identityLight is 1.0.

		tr.overbrightBits is forced to 0 in a window without an offscreen
		target, so identityLight was 1.0 for years and the two spaces coincided.
		r_rts provides that target, overbright goes to 1, identityLight becomes
		0.5 - and the dynamic light term stays where it was, twice as strong
		relative to the ambient floor it is summed against.

		What that looks like is not "dynamic lights are bright". It is
		RB_CalcDiffuseColor dropping every vertex whose normal faces away from
		the light to ambient only, against a directed term that is now double -
		so a grenade under a walkway lights the underside and takes the top of
		it to near black, in a circle that shrinks as the explosion's radius
		decays, because power goes as radius squared.

		The direction pull below is stock behaviour and stays: one close light
		does overwhelm the map's light direction for an entity, and that is how
		Quake has always shaded a model next to an explosion. It was survivable
		at the old contrast, and it is this line that changed the contrast.
		*/
		VectorMA( ent->directedLight, d * tr.identityLight, dl->color, ent->directedLight );
		VectorMA( lightDir, d, dir, lightDir );
	}

	// clamp ambient
	for ( i = 0 ; i < 3 ; i++ ) {
		if ( ent->ambientLight[i] > tr.identityLightByte ) {
			ent->ambientLight[i] = tr.identityLightByte;
		}
	}

	if ( r_debugLight->integer ) {
		LogLight( ent );
	}

	// save out the byte packet version
	((byte *)&ent->ambientLightInt)[0] = myftol( ent->ambientLight[0] ); // -EC-: don't use ri.ftol to avoid precision losses
	((byte *)&ent->ambientLightInt)[1] = myftol( ent->ambientLight[1] );
	((byte *)&ent->ambientLightInt)[2] = myftol( ent->ambientLight[2] );
	((byte *)&ent->ambientLightInt)[3] = 0xff;

	// transform the direction to local space
	VectorNormalize( lightDir );
	ent->lightDir[0] = DotProduct( lightDir, ent->e.axis[0] );
	ent->lightDir[1] = DotProduct( lightDir, ent->e.axis[1] );
	ent->lightDir[2] = DotProduct( lightDir, ent->e.axis[2] );

#ifdef USE_PMLIGHT
	if ( R_STENCIL_SHADOWS && r_dlightMode->integer == 2 ) {
		VectorNormalize( shadowLightDir );
		ent->shadowLightDir[0] = DotProduct( shadowLightDir, ent->e.axis[0] );
		ent->shadowLightDir[1] = DotProduct( shadowLightDir, ent->e.axis[1] );
		ent->shadowLightDir[2] = DotProduct( shadowLightDir, ent->e.axis[2] );
	}
#endif
}


/*
=================
R_LightForPoint
=================
*/
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	trRefEntity_t ent;

	if ( tr.world->lightGridData == NULL )
	  return qfalse;

	Com_Memset(&ent, 0, sizeof(ent));
	VectorCopy( point, ent.e.origin );
	R_SetupEntityLightingGrid( &ent );
	VectorCopy(ent.ambientLight, ambientLight);
	VectorCopy(ent.directedLight, directedLight);
	VectorCopy(ent.lightDir, lightDir);

	return qtrue;
}


/*
=================
R_ShadowRayReach

[QL] E172. How far a model's shadow ray toward the
grid's light may run before what it meets is more likely the light's own
surroundings than something in front of it.

The same two rules the traced shadows on the level use (actorshadow.tmpl),
done here on the CPU because the model pass has no light grid on the GPU:

- Where neighbouring grid points' directions converge, that is the light; the
  ray gets 55% of the way there less 16 units, since the estimate runs a
  median 37% long (E170, measured against the lightmap).
- Where they do not, march toward the light through the grid and stop before
  the ray leaves open space (every sample around it inside a wall - the
  room's shell) or passes the light (the direction there points back).

origin and dir are world space; the answer is at most reach.
=================
*/
static qboolean R_GridDirAt( const vec3_t p, vec3_t dir )
{
	vec3_t amb, dl;

	if ( !R_LightForPoint( (float *)p, amb, dl, dir ) ) {
		return qfalse;
	}
	if ( amb[0] + amb[1] + amb[2] + dl[0] + dl[1] + dl[2] <= 0.0f ) {
		return qfalse;   // every surrounding point inside a wall
	}
	return VectorNormalize( dir ) > 0.0f ? qtrue : qfalse;
}

float R_ShadowRayReach( const vec3_t origin, const vec3_t dir, float reach )
{
	vec3_t up, t1, t2, q, d;
	float sum = 0.0f, s;
	int k, n = 0;

	if ( tr.world == NULL || tr.world->lightGridData == NULL ) {
		return reach;
	}
	/* [QL] E176: the light field, where it has an answer for this point that
	   agrees with the direction - the same light the shadows on the level use */
	{
		vec3_t lp, v;
		float len, sky;
		if ( R_LightFieldAt( origin, lp, &sky ) ) {
			/* [QL] E178: sky or sun - parallel light from outside the level,
			   with nothing of its own on the way for the ray to run into */
			if ( sky >= 0.5f ) {
				return reach;
			}
			VectorSubtract( lp, origin, v );
			len = VectorLength( v );
			if ( len > 1.0f && DotProduct( v, dir ) > 0.7f * len && len < reach ) {
				len = len * 0.55f - 16.0f;
				return len < 16.0f ? 16.0f : len;
			}
		}
	}

	VectorSet( up, 0, 0, 1 );
	if ( fabsf( dir[2] ) >= 0.9f ) {
		VectorSet( up, 1, 0, 0 );
	}
	CrossProduct( dir, up, t1 );
	VectorNormalize( t1 );
	CrossProduct( dir, t1, t2 );

	for ( k = 0; k < 4; k++ ) {
		const float *t = ( k & 1 ) ? t2 : t1;
		vec3_t w;
		float b, denom, s0;

		VectorMA( origin, ( k & 2 ) ? -48.0f : 48.0f, t, q );
		if ( !R_GridDirAt( q, d ) ) {
			continue;
		}
		b = DotProduct( dir, d );
		denom = 1.0f - b * b;
		if ( denom < 0.004f ) {
			continue;
		}
		VectorSubtract( origin, q, w );
		s0 = ( b * DotProduct( d, w ) - DotProduct( dir, w ) ) / denom;
		if ( s0 > 16.0f && s0 < 2.0f * reach ) {
			sum += s0;
			n++;
		}
	}
	if ( n >= 2 && sum / n < reach ) {
		s = ( sum / n ) * 0.55f - 16.0f;
		return s < 16.0f ? 16.0f : s;
	}

	for ( s = 32.0f; s <= reach; s += 32.0f ) {
		VectorMA( origin, s, dir, q );
		if ( !R_GridDirAt( q, d ) ) {
			return s - 32.0f < 16.0f ? 16.0f : s - 32.0f;   // into the room's shell
		}
		if ( DotProduct( d, dir ) < 0.0f ) {
			return s * 0.8f;                               // past the light
		}
	}
	return reach;
}


/*
=================
R_BuildLightField

[QL] E176. Where each light grid point's light is, worked out once per map.

The grid stores a direction toward the dominant light at every point and no
distance. The traced shadows used to estimate the distance per pixel, from where
the directions of four nearby samples crossed (E167). That answer jumped from
pixel to pixel - found here, not found a few pixels over, a different
convergence past a grid boundary - and one player's shadow, cast from two light
positions at once, came apart into pieces with gaps between them (tester's
screenshots, E175 round).

Here, per grid point: the point nearest, in the least-squares sense, to its own
direction line and those of its 26 neighbours that point the same way (within
about 37 degrees), each weighted by its directed light. A small pull toward
512 units along its own direction keeps a point whose neighbours are all
parallel - a distant light - from running off. Then two rounds of averaging
with like-directed neighbours. The shader interpolates the result between grid
points, so the light moves smoothly across a floor instead of jumping.

Measured on japanesecastles against the lanterns and torches: median error -34
units (the four-sample estimate was +91), and neighbouring points differ by a
median 24 units after smoothing (50 before it).

Stored as LF_STRIDE floats a point, with what LF_Classify (E178) adds - the
layout is described there.
=================
*/
#define LF_REACH	512.0f
#define LF_AGREE	0.8f	/* cos ~37 degrees: neighbours that see the same light */
#define LF_SMOOTH	0.9f

static void LF_Dir( const byte *g, vec3_t d, float *directed, qboolean *valid )
{
	const float lat = g[7] * ( 2.0f * M_PI / 256.0f );
	const float lng = g[6] * ( 2.0f * M_PI / 256.0f );
	const int sum = g[0] + g[1] + g[2] + g[3] + g[4] + g[5];

	d[0] = cosf( lat ) * sinf( lng );
	d[1] = sinf( lat ) * sinf( lng );
	d[2] = cosf( lng );
	*directed = 0.299f * g[3] + 0.587f * g[4] + 0.114f * g[5];
	*valid = ( sum > 0 && *directed > 2.0f ) ? qtrue : qfalse;
}

/*
=================
LF_Classify

[QL] E178: the lamp list's first half - whose light each grid point's is.

The grid stores one direction per point and nothing about what kind of light
is at the end of it, and the light field above answers as if it were always a
lamp: the lines of neighbouring points meet somewhere, and that is the light.
On japanesecastles that is wrong for most of the map. Traced against the level
(explain2.py, a CPU replica), 65% of the grid's directed light leads out to
open sky, 10% of it within 20 degrees of the sky shader's q3map_sun, 5% to
glowing surfaces and 20% to nothing left in the map - the lanterns' point
lights, compiled out. Sky light is parallel: there is no position to find,
and a shadow cast from one 300 units overhead points the wrong way and stops
short.

So each point's own direction is traced here, once, against the same level the
GPU traces - the opaque surfaces rt_surface_is_occluder keeps - plus the sky's
surfaces, which the GPU structure leaves out and which are the answer here.
Five rays, the direction and four about five degrees around it, so a point
beside a window frame is not decided by one grazing ray; the share that reaches
the sky is the point's sky fraction, then averaged once with like-directed
neighbours. A lamp's point keeps its light field position, now clamped to no
further than the first thing its direction runs into - the light cannot be
behind the wall it is lighting this point through. A sky point's position goes
LF_SKY units out along its direction, which the shaders read as a direction,
not a place.

Separately, for every open point, whether the exact q3map_sun direction reaches
the sky from there. That is the second light (the sun alongside a lamp that
out-shines it in the grid), and the sun's own directed brightness is the median
of the grid where the sun dominates, from the map's own numbers rather than a
guess at q3map2's units.

Layout, LF_STRIDE floats a point:
	[0..2] light position (lamp) or LF_SKY out along the direction (sky)
	[3]    1 if [0..2] means something, else 0
	[4]    1 if the sun reaches this point
	[5]    sky fraction 0..1
	[6]    trust 0..1 (E190): how much of the light at [0..2] its grid point
	       can see past the level - see LF_Trust
	[7]    1 if the point is open (not inside a wall)
and after the last point, four floats: the sun's direction and its directed
brightness 0..1 (all zero on a map with no q3map_sun).
=================
*/
#define LF_SKY		4096.0f
#define LF_SUN_COS	0.94f	/* cos 20 degrees: near enough the sun to be it */

typedef struct {
	vec3_t	v0, e1, e2;
	int		sky;
} lfTri_t;

typedef struct {
	vec3_t	mins, maxs;
	int		first, count;	/* count > 0: a leaf over tris [first..]; else children first, first + 1 */
} lfNode_t;

typedef struct {
	lfTri_t		*tris;
	int			*order;
	lfNode_t	*nodes;
	int			numTris, numNodes;
} lfBvh_t;

static int LF_SurfaceKind( const msurface_t *surf )
{
	const shader_t *sh = surf->shader;
	int i;

	if ( surf->data == NULL || sh == NULL ) {
		return 0;
	}
	if ( sh->isSky || ( sh->surfaceFlags & SURF_SKY ) ) {
		return 2;
	}
	/* the same rules as vk_rt_world.c's rt_surface_is_occluder, so this traces the
	   level the GPU does */
	if ( sh->sort != SS_OPAQUE || ( sh->surfaceFlags & ( SURF_NODRAW | SURF_NONSOLID ) ) ) {
		return 0;
	}
	for ( i = 0; i < sh->numUnfoggedPasses; i++ ) {
		const shaderStage_t *st = sh->stages[i];
		if ( st && st->active && ( st->stateBits & GLS_ATEST_BITS ) ) {
			return 0;
		}
	}
	return 1;
}

/* the surface's triangles into tris (NULL: count only); returns how many */
static int LF_SurfaceTris( const msurface_t *surf, int sky, lfTri_t *tris )
{
	const surfaceType_t *data = surf->data;
	int n = 0, i, x, y;

#define LF_ADD( A, B, C ) do { if ( tris ) { lfTri_t *lt_ = &tris[n]; VectorCopy( A, lt_->v0 ); \
		VectorSubtract( B, A, lt_->e1 ); VectorSubtract( C, A, lt_->e2 ); lt_->sky = sky; } n++; } while ( 0 )

	switch ( *data ) {
	case SF_FACE: {
		const srfSurfaceFace_t *f = (const srfSurfaceFace_t *)data;
		const unsigned *ind = (const unsigned *)( (const byte *)f + f->ofsIndices );
		for ( i = 0; i + 2 < f->numIndices; i += 3 ) {
			LF_ADD( f->points[ind[i]], f->points[ind[i+1]], f->points[ind[i+2]] );
		}
		break;
	}
	case SF_TRIANGLES: {
		const srfTriangles_t *t = (const srfTriangles_t *)data;
		for ( i = 0; i + 2 < t->numIndexes; i += 3 ) {
			LF_ADD( t->verts[t->indexes[i]].xyz, t->verts[t->indexes[i+1]].xyz, t->verts[t->indexes[i+2]].xyz );
		}
		break;
	}
	case SF_GRID: {
		const srfGridMesh_t *g = (const srfGridMesh_t *)data;
		for ( y = 0; y < g->height - 1; y++ ) {
			for ( x = 0; x < g->width - 1; x++ ) {
				const float *a = g->verts[y * g->width + x].xyz, *b = g->verts[y * g->width + x + 1].xyz;
				const float *c = g->verts[( y + 1 ) * g->width + x].xyz, *d = g->verts[( y + 1 ) * g->width + x + 1].xyz;
				LF_ADD( a, c, b );
				LF_ADD( b, c, d );
			}
		}
		break;
	}
	default:
		break;
	}
#undef LF_ADD
	return n;
}

static const lfTri_t *lfSortTris;
static int lfSortAxis;

static int LF_CompareTris( const void *a, const void *b )
{
	const lfTri_t *ta = &lfSortTris[*(const int *)a], *tb = &lfSortTris[*(const int *)b];
	/* centroid * 3, less v0 * 3 - the same order */
	const float ca = 3.0f * ta->v0[lfSortAxis] + ta->e1[lfSortAxis] + ta->e2[lfSortAxis];
	const float cb = 3.0f * tb->v0[lfSortAxis] + tb->e1[lfSortAxis] + tb->e2[lfSortAxis];
	return ca < cb ? -1 : ca > cb ? 1 : 0;
}

static void LF_TriBounds( const lfTri_t *t, vec3_t mins, vec3_t maxs )
{
	int k;
	for ( k = 0; k < 3; k++ ) {
		const float a = t->v0[k], b = a + t->e1[k], c = a + t->e2[k];
		const float lo = a < b ? ( a < c ? a : c ) : ( b < c ? b : c );
		const float hi = a > b ? ( a > c ? a : c ) : ( b > c ? b : c );
		if ( lo < mins[k] ) mins[k] = lo;
		if ( hi > maxs[k] ) maxs[k] = hi;
	}
}

/* median split on the longest axis, four triangles a leaf */
static qboolean LF_BuildBvh( lfBvh_t *b )
{
	int stack[128], sp = 0, i;

	b->nodes = ri.Malloc( ( 2 * b->numTris + 1 ) * sizeof( lfNode_t ) );
	b->order = ri.Malloc( b->numTris * sizeof( int ) );
	if ( b->nodes == NULL || b->order == NULL ) {
		return qfalse;
	}
	for ( i = 0; i < b->numTris; i++ ) {
		b->order[i] = i;
	}
	b->numNodes = 1;
	stack[sp++] = 0;
	b->nodes[0].first = 0;
	b->nodes[0].count = b->numTris;
	lfSortTris = b->tris;

	while ( sp > 0 ) {
		lfNode_t *nd = &b->nodes[ stack[--sp] ];
		const int first = nd->first, count = nd->count;
		vec3_t ext;
		int axis, half;

		ClearBounds( nd->mins, nd->maxs );
		for ( i = 0; i < count; i++ ) {
			LF_TriBounds( &b->tris[ b->order[first + i] ], nd->mins, nd->maxs );
		}
		if ( count <= 4 || sp >= 126 ) {
			continue;
		}
		VectorSubtract( nd->maxs, nd->mins, ext );
		axis = ext[0] > ext[1] ? ( ext[0] > ext[2] ? 0 : 2 ) : ( ext[1] > ext[2] ? 1 : 2 );
		lfSortAxis = axis;
		qsort( b->order + first, count, sizeof( int ), LF_CompareTris );
		half = count / 2;

		nd->first = b->numNodes;
		nd->count = 0;
		b->nodes[b->numNodes].first = first;
		b->nodes[b->numNodes].count = half;
		b->nodes[b->numNodes + 1].first = first + half;
		b->nodes[b->numNodes + 1].count = count - half;
		stack[sp++] = b->numNodes;
		stack[sp++] = b->numNodes + 1;
		b->numNodes += 2;
	}
	return qtrue;
}

static qboolean LF_RayBox( const lfNode_t *nd, const vec3_t o, const vec3_t inv, float tmax )
{
	float t0 = 0.0f, t1 = tmax;
	int k;
	for ( k = 0; k < 3; k++ ) {
		float a = ( nd->mins[k] - o[k] ) * inv[k];
		float c = ( nd->maxs[k] - o[k] ) * inv[k];
		if ( a > c ) { const float s = a; a = c; c = s; }
		if ( a > t0 ) t0 = a;
		if ( c < t1 ) t1 = c;
		if ( t0 > t1 ) return qfalse;
	}
	return qtrue;
}

/* the nearest triangle along d from o within tmax: its distance, and whether
   it is sky. Nothing hit - out through a leak, or a grid point outside the
   hull - counts as sky too: nothing of the level is in the way. */
static float LF_Trace( const lfBvh_t *b, const vec3_t o, const vec3_t d, float tmax, int *sky )
{
	int stack[64], sp = 0;
	vec3_t inv;
	float best = tmax;
	int k;

	*sky = 1;
	for ( k = 0; k < 3; k++ ) {
		inv[k] = fabsf( d[k] ) > 1e-8f ? 1.0f / d[k] : ( d[k] < 0.0f ? -1e8f : 1e8f );
	}
	stack[sp++] = 0;
	while ( sp > 0 ) {
		const lfNode_t *nd = &b->nodes[ stack[--sp] ];
		if ( !LF_RayBox( nd, o, inv, best ) ) {
			continue;
		}
		if ( nd->count == 0 ) {
			if ( sp < 62 ) {
				stack[sp++] = nd->first;
				stack[sp++] = nd->first + 1;
			}
			continue;
		}
		for ( k = 0; k < nd->count; k++ ) {
			const lfTri_t *t = &b->tris[ b->order[nd->first + k] ];
			vec3_t p, s, q;
			float det, inv_det, u, v, tt;
			CrossProduct( d, t->e2, p );
			det = DotProduct( t->e1, p );
			if ( fabsf( det ) < 1e-9f ) continue;
			inv_det = 1.0f / det;
			VectorSubtract( o, t->v0, s );
			u = DotProduct( s, p ) * inv_det;
			if ( u < 0.0f || u > 1.0f ) continue;
			CrossProduct( s, t->e1, q );
			v = DotProduct( d, q ) * inv_det;
			if ( v < 0.0f || u + v > 1.0f ) continue;
			tt = DotProduct( t->e2, q ) * inv_det;
			if ( tt > 0.5f && tt < best ) {
				best = tt;
				*sky = t->sky;
			}
		}
	}
	return best;
}

/*
[QL] E190: how far a lamp point's estimated light is to be believed.

The map compiler built the light grid with occlusion: a grid point's
directed light came from a lamp that point can see. So a light field estimate
the point CANNOT see - the level is in the way - is wrong, and every shadow
cast from it is wrong too: the column's curved shadow across the flag room's
tatami, the jagged edges in the blue base, shadows the lightmap does not have.

Five rays from the point, at the estimated light and at four points 12 units
round it, against the level, stopping 24 short so the lamp's own fitting does
not count. The share that get through is the point's trust; level shadows
are scaled by it (actorshadow.tmpl), so a point whose light is a guess leaves
the lightmap's own shadow standing instead of drawing a second, wrong one.
Players' and items' shadows are not scaled: a few degrees off on those reads
as a shadow, on a column it reads as a mistake.
*/
static float LF_Trust( const lfBvh_t *b, const vec3_t pc, const vec3_t light )
{
	vec3_t v, t1, t2, up, tgt, d;
	float dist, seen = 0.0f;
	int k, sky;

	VectorSubtract( light, pc, v );
	dist = VectorNormalize( v );
	if ( dist < 32.0f ) {
		return 1.0f;
	}
	VectorSet( up, 0, 0, 1 );
	if ( fabsf( v[2] ) >= 0.9f ) VectorSet( up, 1, 0, 0 );
	CrossProduct( v, up, t1 );
	VectorNormalize( t1 );
	CrossProduct( v, t1, t2 );
	for ( k = 0; k < 5; k++ ) {
		float len, t;
		VectorCopy( light, tgt );
		if ( k > 0 ) {
			VectorMA( tgt, ( k & 2 ) ? -12.0f : 12.0f, ( k & 1 ) ? t2 : t1, tgt );
		}
		VectorSubtract( tgt, pc, d );
		len = VectorNormalize( d );
		t = LF_Trace( b, pc, d, len - 24.0f, &sky );
		if ( t >= len - 24.0f ) {
			seen += 1.0f;
		}
	}
	return seen / 5.0f;
}

static int LF_CompareFloats( const void *a, const void *b )
{
	const float fa = *(const float *)a, fb = *(const float *)b;
	return fa < fb ? -1 : fa > fb ? 1 : 0;
}

static void LF_Classify( const world_t *w, const float *dir, const float *wt, const byte *ok,
	const float *field, float *out )
{
	const int bx = w->lightGridBounds[0], by = w->lightGridBounds[1], bz = w->lightGridBounds[2];
	const int n = bx * by * bz;
	const msurface_t *surfs = w->surfaces;
	const qboolean haveSun = VectorLength( tr.sunLight ) > 0.0f ? qtrue : qfalse;
	int numSurfs = w->numsurfaces, i, x, y, z, s;
	int nSky = 0, nLamp = 0, nSunVis = 0, nSunPts = 0, nSeen = 0;
	float trustSum = 0.0f;
	float *skyRaw, *sunSamples;
	lfBvh_t b;
	int start = ri.Milliseconds();

	Com_Memset( out, 0, ( n * LF_STRIDE + 4 ) * sizeof( float ) );
	Com_Memset( &b, 0, sizeof( b ) );

	/* the static level, as vk_rt_world.c builds it: submodel 0 */
	if ( w->bmodels != NULL && w->bmodels[0].numSurfaces > 0 && w->bmodels[0].numSurfaces <= w->numsurfaces ) {
		surfs = w->bmodels[0].firstSurface;
		numSurfs = w->bmodels[0].numSurfaces;
	}
	for ( s = 0; s < numSurfs; s++ ) {
		const int kind = LF_SurfaceKind( &surfs[s] );
		if ( kind ) {
			b.numTris += LF_SurfaceTris( &surfs[s], kind == 2, NULL );
		}
	}
	skyRaw = ri.Malloc( n * sizeof( float ) );
	sunSamples = ri.Malloc( n * sizeof( float ) );
	if ( b.numTris > 0 ) {
		b.tris = ri.Malloc( b.numTris * sizeof( lfTri_t ) );
	}
	if ( b.tris == NULL || skyRaw == NULL || sunSamples == NULL ) {
		/* nothing to trace against: every located light stays a lamp, as in E176 */
		for ( i = 0; i < n; i++ ) {
			VectorCopy( field + i * 4, out + i * LF_STRIDE );
			out[i * LF_STRIDE + 3] = field[i * 4 + 3];
		}
		goto done;
	}
	b.numTris = 0;
	for ( s = 0; s < numSurfs; s++ ) {
		const int kind = LF_SurfaceKind( &surfs[s] );
		if ( kind ) {
			b.numTris += LF_SurfaceTris( &surfs[s], kind == 2, b.tris + b.numTris );
		}
	}
	if ( !LF_BuildBvh( &b ) ) {
		for ( i = 0; i < n; i++ ) {
			VectorCopy( field + i * 4, out + i * LF_STRIDE );
			out[i * LF_STRIDE + 3] = field[i * 4 + 3];
		}
		goto done;
	}

#define LF_IDX( X, Y, Z ) ( ( Z ) * bx * by + ( Y ) * bx + ( X ) )
	for ( z = 0; z < bz; z++ ) for ( y = 0; y < by; y++ ) for ( x = 0; x < bx; x++ ) {
		const int c = LF_IDX( x, y, z );
		const byte *g = w->lightGridData + c * 8;
		float *o = out + c * LF_STRIDE;
		vec3_t pc;
		int sky, k;

		skyRaw[c] = -1.0f;
		if ( g[0] + g[1] + g[2] + g[3] + g[4] + g[5] == 0 ) {
			continue;   /* inside a wall */
		}
		pc[0] = w->lightGridOrigin[0] + x * w->lightGridSize[0];
		pc[1] = w->lightGridOrigin[1] + y * w->lightGridSize[1];
		pc[2] = w->lightGridOrigin[2] + z * w->lightGridSize[2];
		o[7] = 1.0f;

		if ( haveSun ) {
			LF_Trace( &b, pc, tr.sunDirection, 16384.0f, &sky );
			if ( sky ) {
				o[4] = 1.0f;
				nSunVis++;
			}
		}
		if ( !ok[c] ) {
			continue;
		}
		{
			const float *d = dir + c * 3;
			vec3_t up, t1, t2, dd;
			float hits = 0.0f;
			VectorSet( up, 0, 0, 1 );
			if ( fabsf( d[2] ) >= 0.9f ) VectorSet( up, 1, 0, 0 );
			CrossProduct( d, up, t1 );
			VectorNormalize( t1 );
			CrossProduct( d, t1, t2 );
			LF_Trace( &b, pc, d, 16384.0f, &sky );
			hits += sky;
			for ( k = 0; k < 4; k++ ) {
				VectorMA( d, ( k & 2 ) ? -0.09f : 0.09f, ( k & 1 ) ? t2 : t1, dd );
				VectorNormalize( dd );
				LF_Trace( &b, pc, dd, 16384.0f, &sky );
				hits += sky;
			}
			skyRaw[c] = hits / 5.0f;
		}
	}

	/* once around with like-directed neighbours, then decide */
	for ( z = 0; z < bz; z++ ) for ( y = 0; y < by; y++ ) for ( x = 0; x < bx; x++ ) {
		const int c = LF_IDX( x, y, z );
		const float *d = dir + c * 3;
		float *o = out + c * LF_STRIDE;
		float acc = 0.0f, sw = 0.0f, sk;
		int dx, dy, dz;
		vec3_t pc;

		if ( skyRaw[c] < 0.0f ) {
			continue;
		}
		for ( dz = -1; dz <= 1; dz++ ) for ( dy = -1; dy <= 1; dy++ ) for ( dx = -1; dx <= 1; dx++ ) {
			const int nx = x + dx, ny = y + dy, nz = z + dz;
			int m;
			float ww;
			if ( nx < 0 || ny < 0 || nz < 0 || nx >= bx || ny >= by || nz >= bz ) continue;
			m = LF_IDX( nx, ny, nz );
			if ( skyRaw[m] < 0.0f || DotProduct( dir + m * 3, d ) < LF_SMOOTH ) continue;
			ww = ( dx | dy | dz ) ? 1.0f : 2.0f;
			acc += ww * skyRaw[m];
			sw += ww;
		}
		sk = acc / sw;
		o[5] = sk;
		pc[0] = w->lightGridOrigin[0] + x * w->lightGridSize[0];
		pc[1] = w->lightGridOrigin[1] + y * w->lightGridSize[1];
		pc[2] = w->lightGridOrigin[2] + z * w->lightGridSize[2];

		if ( sk >= 0.5f ) {
			VectorMA( pc, LF_SKY, d, o );
			o[3] = 1.0f;
			nSky++;
			if ( haveSun && o[4] > 0.0f && DotProduct( d, tr.sunDirection ) > LF_SUN_COS ) {
				sunSamples[nSunPts++] = wt[c] / 255.0f;
			}
		} else if ( field[c * 4 + 3] > 0.0f ) {
			/* [QL] E183: the lamp's position as the field found it. E178 also
			   clamped it to the first wall its direction meets, which measured
			   worse - median error 26 units without, 66 with, against the
			   map's own fittings - because a blended direction runs into the
			   lamp's own fitting or a nearby edge first. Near walls and
			   corners that put the light far too close and bent shadows
			   there. */
			VectorCopy( field + c * 4, o );
			o[3] = 1.0f;
			o[6] = LF_Trust( &b, pc, o );   /* E190 */
			trustSum += o[6];
			if ( o[6] >= 0.6f ) nSeen++;
			nLamp++;
		} else {
			o[6] = 1.0f;
		}
	}
#undef LF_IDX

	/* E190: the score - how many lamp points can see the light they were given */
	ri.Printf( PRINT_ALL, "Light field: %i of %i lamp points (%.1f%%) see their estimated light, mean trust %.2f\n",
		nSeen, nLamp, nLamp ? 100.0f * nSeen / nLamp : 0.0f, nLamp ? trustSum / nLamp : 0.0f );

	if ( nSunPts >= 32 ) {
		float *tail = out + n * LF_STRIDE;
		qsort( sunSamples, nSunPts, sizeof( float ), LF_CompareFloats );
		VectorCopy( tr.sunDirection, tail );
		tail[3] = sunSamples[nSunPts / 2];
	}

	ri.Printf( PRINT_ALL, "Light field: %i sky, %i lamp, sun reaches %i points, "
		"sun brightness %.2f from %i; %i triangles, %i ms\n",
		nSky, nLamp, nSunVis, out[n * LF_STRIDE + 3], nSunPts, b.numTris, ri.Milliseconds() - start );

done:
	if ( b.tris ) ri.Free( b.tris );
	if ( b.nodes ) ri.Free( b.nodes );
	if ( b.order ) ri.Free( b.order );
	if ( skyRaw ) ri.Free( skyRaw );
	if ( sunSamples ) ri.Free( sunSamples );
}

void R_BuildLightField( world_t *w )
{
	const int bx = w->lightGridBounds[0], by = w->lightGridBounds[1], bz = w->lightGridBounds[2];
	const int n = bx * by * bz;
	float *dir, *wt, *pos, *tmp, *out;
	byte *ok;
	int x, y, z, i, it, found = 0;

	w->lightField = NULL;
	if ( w->lightGridData == NULL || n <= 0 ) {
		return;
	}

	dir = ri.Malloc( n * 3 * sizeof( float ) );
	wt = ri.Malloc( n * sizeof( float ) );
	ok = ri.Malloc( n );
	tmp = ri.Malloc( n * 4 * sizeof( float ) );
	pos = ri.Malloc( n * 4 * sizeof( float ) );

	for ( i = 0; i < n; i++ ) {
		qboolean v;
		LF_Dir( w->lightGridData + i * 8, dir + i * 3, &wt[i], &v );
		ok[i] = v;
	}

#define LF_IDX( X, Y, Z ) ( ( Z ) * bx * by + ( Y ) * bx + ( X ) )
#define LF_POINT( X, Y, Z, out ) ( out[0] = w->lightGridOrigin[0] + ( X ) * w->lightGridSize[0], \
	out[1] = w->lightGridOrigin[1] + ( Y ) * w->lightGridSize[1], \
	out[2] = w->lightGridOrigin[2] + ( Z ) * w->lightGridSize[2] )

	/* least squares: the point nearest to the like-directed lines around */
	for ( z = 0; z < bz; z++ ) for ( y = 0; y < by; y++ ) for ( x = 0; x < bx; x++ ) {
		const int c = LF_IDX( x, y, z );
		const float *dc = dir + c * 3;
		double A[3][3] = { { 0 } }, b[3] = { 0 }, det, inv[3][3];
		vec3_t pc, X;
		int dx, dy, dz, k, j;

		tmp[c*4+0] = tmp[c*4+1] = tmp[c*4+2] = tmp[c*4+3] = 0.0f;
		if ( !ok[c] ) {
			continue;
		}
		for ( dz = -1; dz <= 1; dz++ ) for ( dy = -1; dy <= 1; dy++ ) for ( dx = -1; dx <= 1; dx++ ) {
			const int nx = x + dx, ny = y + dy, nz = z + dz;
			int m;
			const float *dn;
			vec3_t pn;
			float ww;

			if ( nx < 0 || ny < 0 || nz < 0 || nx >= bx || ny >= by || nz >= bz ) continue;
			m = LF_IDX( nx, ny, nz );
			if ( !ok[m] ) continue;
			dn = dir + m * 3;
			if ( DotProduct( dn, dc ) < LF_AGREE ) continue;
			ww = ( wt[m] > 255.0f ? 255.0f : wt[m] ) / 255.0f;
			LF_POINT( nx, ny, nz, pn );
			for ( k = 0; k < 3; k++ ) {
				double row = 0.0;
				for ( j = 0; j < 3; j++ ) {
					const double mkj = ( k == j ? 1.0 : 0.0 ) - dn[k] * dn[j];
					A[k][j] += ww * mkj;
					row += mkj * pn[j];
				}
				b[k] += ww * row;
			}
		}
		LF_POINT( x, y, z, pc );
		for ( k = 0; k < 3; k++ ) {   /* the pull toward LF_REACH along its own line */
			A[k][k] += 1e-3;
			b[k] += 1e-3 * ( pc[k] + dc[k] * LF_REACH );
		}
		det = A[0][0] * ( A[1][1] * A[2][2] - A[1][2] * A[2][1] )
		    - A[0][1] * ( A[1][0] * A[2][2] - A[1][2] * A[2][0] )
		    + A[0][2] * ( A[1][0] * A[2][1] - A[1][1] * A[2][0] );
		if ( fabs( det ) < 1e-12 ) {
			continue;
		}
		inv[0][0] =  ( A[1][1] * A[2][2] - A[1][2] * A[2][1] ) / det;
		inv[0][1] = -( A[0][1] * A[2][2] - A[0][2] * A[2][1] ) / det;
		inv[0][2] =  ( A[0][1] * A[1][2] - A[0][2] * A[1][1] ) / det;
		inv[1][0] = -( A[1][0] * A[2][2] - A[1][2] * A[2][0] ) / det;
		inv[1][1] =  ( A[0][0] * A[2][2] - A[0][2] * A[2][0] ) / det;
		inv[1][2] = -( A[0][0] * A[1][2] - A[0][2] * A[1][0] ) / det;
		inv[2][0] =  ( A[1][0] * A[2][1] - A[1][1] * A[2][0] ) / det;
		inv[2][1] = -( A[0][0] * A[2][1] - A[0][1] * A[2][0] ) / det;
		inv[2][2] =  ( A[0][0] * A[1][1] - A[0][1] * A[1][0] ) / det;
		for ( k = 0; k < 3; k++ ) {
			X[k] = (float)( inv[k][0] * b[0] + inv[k][1] * b[1] + inv[k][2] * b[2] );
		}
		/* in front of the point, and not beyond the reach */
		{
			vec3_t v;
			float s;
			VectorSubtract( X, pc, v );
			s = DotProduct( v, dc );
			if ( s < 16.0f || s > 4.0f * LF_REACH ) {
				VectorMA( pc, LF_REACH, dc, X );
			}
		}
		VectorCopy( X, tmp + c * 4 );
		tmp[c*4+3] = 1.0f;
	}

	/* two rounds of averaging with neighbours that see the same light */
	for ( it = 0; it < 2; it++ ) {
		float *src = ( it == 0 ) ? tmp : pos;
		float *dst = ( it == 0 ) ? pos : tmp;
		for ( z = 0; z < bz; z++ ) for ( y = 0; y < by; y++ ) for ( x = 0; x < bx; x++ ) {
			const int c = LF_IDX( x, y, z );
			vec3_t acc = { 0, 0, 0 };
			float sw = 0.0f;
			int dx, dy, dz;

			if ( src[c*4+3] <= 0.0f ) {
				dst[c*4+0] = dst[c*4+1] = dst[c*4+2] = dst[c*4+3] = 0.0f;
				continue;
			}
			for ( dz = -1; dz <= 1; dz++ ) for ( dy = -1; dy <= 1; dy++ ) for ( dx = -1; dx <= 1; dx++ ) {
				const int nx = x + dx, ny = y + dy, nz = z + dz;
				int m;
				float ww;
				if ( nx < 0 || ny < 0 || nz < 0 || nx >= bx || ny >= by || nz >= bz ) continue;
				m = LF_IDX( nx, ny, nz );
				if ( src[m*4+3] <= 0.0f || DotProduct( dir + m * 3, dir + c * 3 ) < LF_SMOOTH ) continue;
				ww = ( dx | dy | dz ) ? 1.0f : 2.0f;
				VectorMA( acc, ww, src + m * 4, acc );
				sw += ww;
			}
			VectorScale( acc, 1.0f / sw, dst + c * 4 );
			dst[c*4+3] = 1.0f;
		}
	}
	/* after two rounds the result is in tmp */
	for ( i = 0; i < n; i++ ) {
		if ( tmp[i*4+3] > 0.0f ) found++;
	}
	ri.Printf( PRINT_ALL, "Light field: %i of %i grid points locate their light\n", found, n );

	/* [QL] E178: which of them is the sky's, and where the sun reaches */
	out = ri.Hunk_Alloc( ( n * LF_STRIDE + 4 ) * sizeof( float ), h_low );
	LF_Classify( w, dir, wt, ok, tmp, out );

#undef LF_IDX
#undef LF_POINT

	ri.Free( dir );
	ri.Free( wt );
	ri.Free( ok );
	ri.Free( tmp );
	ri.Free( pos );
	w->lightField = out;
}

/* [QL] E176: the light field, interpolated at p like the grid itself -
   corners with no answer left out and the rest renormalised */
qboolean R_LightFieldAt( const vec3_t p, vec3_t lightPos, float *sky )
{
	const world_t *w = tr.world;
	vec3_t v;
	int i0[3], k, i;
	float f[3], total = 0.0f, skyAcc = 0.0f;

	VectorClear( lightPos );
	if ( sky ) {
		*sky = 0.0f;
	}
	if ( w == NULL || w->lightField == NULL || !r_rtLightField->integer ) {
		return qfalse;
	}
	VectorSubtract( p, w->lightGridOrigin, v );
	for ( k = 0; k < 3; k++ ) {
		const float g = v[k] * w->lightGridInverseSize[k];
		i0[k] = (int)floorf( g );
		f[k] = g - i0[k];
	}
	for ( i = 0; i < 8; i++ ) {
		int c[3], idx;
		float ww = 1.0f;
		const float *lf;
		for ( k = 0; k < 3; k++ ) {
			const int o = ( i >> k ) & 1;
			c[k] = i0[k] + o;
			if ( c[k] < 0 ) c[k] = 0;
			if ( c[k] > w->lightGridBounds[k] - 1 ) c[k] = w->lightGridBounds[k] - 1;
			ww *= o ? f[k] : 1.0f - f[k];
		}
		idx = c[2] * w->lightGridBounds[0] * w->lightGridBounds[1] + c[1] * w->lightGridBounds[0] + c[0];
		lf = w->lightField + idx * LF_STRIDE;
		if ( lf[3] <= 0.0f || ww <= 0.0f ) continue;
		VectorMA( lightPos, ww, lf, lightPos );
		skyAcc += ww * lf[5];
		total += ww;
	}
	if ( total <= 0.0f ) {
		return qfalse;
	}
	VectorScale( lightPos, 1.0f / total, lightPos );
	if ( sky ) {
		*sky = skyAcc / total;
	}
	return qtrue;
}
