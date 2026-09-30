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
		float len;
		if ( R_LightFieldAt( origin, lp ) ) {
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

Stored as four floats a point: the light's position and 1, or zeros where the
point is inside a wall or unlit.
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

void R_BuildLightField( world_t *w )
{
	const int bx = w->lightGridBounds[0], by = w->lightGridBounds[1], bz = w->lightGridBounds[2];
	const int n = bx * by * bz;
	float *dir, *wt, *pos, *tmp;
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
	pos = ri.Hunk_Alloc( n * 4 * sizeof( float ), h_low );

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
	/* after two rounds the result is in tmp: copy it into the kept array */
	Com_Memcpy( pos, tmp, n * 4 * sizeof( float ) );
	for ( i = 0; i < n; i++ ) {
		if ( pos[i*4+3] > 0.0f ) found++;
	}

#undef LF_IDX
#undef LF_POINT

	ri.Free( dir );
	ri.Free( wt );
	ri.Free( ok );
	ri.Free( tmp );
	w->lightField = pos;
	ri.Printf( PRINT_ALL, "Light field: %i of %i grid points locate their light\n", found, n );
}

/* [QL] E176: the light field, interpolated at p like the grid itself -
   corners with no answer left out and the rest renormalised */
qboolean R_LightFieldAt( const vec3_t p, vec3_t lightPos )
{
	const world_t *w = tr.world;
	vec3_t v;
	int i0[3], k, i;
	float f[3], total = 0.0f;

	VectorClear( lightPos );
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
		lf = w->lightField + idx * 4;
		if ( lf[3] <= 0.0f || ww <= 0.0f ) continue;
		VectorMA( lightPos, ww, lf, lightPos );
		total += ww;
	}
	if ( total <= 0.0f ) {
		return qfalse;
	}
	VectorScale( lightPos, 1.0f / total, lightPos );
	return qtrue;
}
