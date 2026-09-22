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

static int			r_firstSceneDrawSurf;
#ifdef USE_PMLIGHT
static int			r_firstSceneLitSurf;
#endif

int			r_numdlights;
static int			r_firstSceneDlight;

static int			r_numentities;
static int			r_firstSceneEntity;

static int			r_numpolys;
static int			r_firstScenePoly;

static int			r_numpolyverts;

/* [QL] Ripples seen since the last world load - see RE_AddWaterRipple. */
static int			r_rippleCount;


/*
====================
R_InitNextFrame

====================
*/
void R_InitNextFrame( void ) {

	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;
#ifdef USE_PMLIGHT
	r_firstSceneLitSurf = 0;
#endif

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	r_firstSceneDlight = r_numdlights;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( void ) {
	int			i;
	shader_t	*sh;
	const srfPoly_t	*poly;

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0, poly = tr.refdef.polys; i < tr.refdef.numPolys ; i++, poly++ ) {
		sh = R_GetShaderByHandle( poly->hShader );
		R_AddDrawSurf( ( void * )poly, sh, poly->fogIndex, 0 );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/
void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
	srfPoly_t	*poly;
	int			i, j;
	int			fogIndex;
	const fog_t		*fog;
	vec3_t		bounds[2];

	if ( !tr.registered ) {
		return;
	}
#if 0
	if ( !hShader ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_AddPolyToScene: NULL poly shader\n");
		return;
	}
#endif
	for ( j = 0; j < numPolys; j++ ) {
		if ( r_numpolyverts + numVerts > max_polyverts || r_numpolys >= max_polys ) {
      /*
      NOTE TTimo this was initially a PRINT_WARNING
      but it happens a lot with high fighting scenes and particles
      since we don't plan on changing the const and making for room for those effects
      simply cut this message to developer only
      */
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->hShader = hShader;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];
		
		Com_Memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );
#if 0
		if ( glConfig.hardwareType == GLHW_RAGEPRO ) {
			poly->verts->modulate[0] = 255;
			poly->verts->modulate[1] = 255;
			poly->verts->modulate[2] = 255;
			poly->verts->modulate[3] = 255;
		}
#endif
		// done.
		r_numpolys++;
		r_numpolyverts += numVerts;

		// if no world is loaded
		if ( tr.world == NULL ) {
			fogIndex = 0;
		}
		// see if it is in a fog volume
		else if ( tr.world->numfogs == 1 ) {
			fogIndex = 0;
		} else {
			// find which fog volume the poly is in
			VectorCopy( poly->verts[0].xyz, bounds[0] );
			VectorCopy( poly->verts[0].xyz, bounds[1] );
			for ( i = 1 ; i < poly->numVerts ; i++ ) {
				AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
			}
			for ( fogIndex = 1 ; fogIndex < tr.world->numfogs ; fogIndex++ ) {
				fog = &tr.world->fogs[fogIndex]; 
				if ( bounds[1][0] >= fog->bounds[0][0]
					&& bounds[1][1] >= fog->bounds[0][1]
					&& bounds[1][2] >= fog->bounds[0][2]
					&& bounds[0][0] <= fog->bounds[1][0]
					&& bounds[0][1] <= fog->bounds[1][1]
					&& bounds[0][2] <= fog->bounds[1][2] ) {
					break;
				}
			}
			if ( fogIndex == tr.world->numfogs ) {
				fogIndex = 0;
			}
		}
		poly->fogIndex = fogIndex;
	}
}


//=================================================================================

static int isnan_fp( const float *f )
{
	uint32_t u = *( (uint32_t*) f );
	u = 0x7F800000 - ( u & 0x7FFFFFFF );
	return (int)( u >> 31 );
}


/*
=====================
RE_AddRefEntityToScene
=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent, qboolean intShaderTime ) {
	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		// [QL] developer-only on purpose; `developer` defaults to 1 in this
		// build (common.c) so it is visible without asking for it, and the
		// release switch is that one default rather than this line.
		ri.Printf( PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n" );
		return;
	}
	if ( isnan_fp( &ent->origin[0] ) || isnan_fp( &ent->origin[1] ) || isnan_fp( &ent->origin[2] ) ) {
		static qboolean first_time = qtrue;
		if ( first_time ) {
			first_time = qfalse;
			ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n" );
		}
		return;
	}
	if ( (unsigned)ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;
	backEndData->entities[r_numentities].intShaderTime = intShaderTime;

	r_numentities++;
}


/*
=====================
RE_AddDynamicLightToScene
=====================
*/
static void RE_AddDynamicLightToScene( const vec3_t org, float intensity, float r, float g, float b, int additive ) {
	dlight_t	*dl;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
#ifndef USE_VULKAN
	// these cards don't have the correct blend mode
	if ( glConfig.hardwareType == GLHW_RIVA128 || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		return;
	}
#endif
#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( r_dlightMode->integer )
#endif
	{
		r *= r_dlightIntensity->value;
		g *= r_dlightIntensity->value;
		b *= r_dlightIntensity->value;
		intensity *= r_dlightScale->value;
	}
#endif

	if ( r_dlightSaturation->value != 1.0 )
	{
		float luminance = LUMA( r, g, b );
		r = LERP( luminance, r, r_dlightSaturation->value );
		g = LERP( luminance, g, r_dlightSaturation->value );
		b = LERP( luminance, b, r_dlightSaturation->value );
	}

	dl = &backEndData->dlights[r_numdlights++];
	VectorCopy( org, dl->origin );
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->additive = additive;
	dl->linear = qfalse;
}


/*
=====================
RE_AddLinearLightToScene
=====================
*/
void RE_AddLinearLightToScene( const vec3_t start, const vec3_t end, float intensity, float r, float g, float b  ) {
	dlight_t	*dl;
	if ( VectorCompare( start, end ) ) {
		RE_AddDynamicLightToScene( start, intensity, r, g, b, 0 );
		return;
	}
	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( r_dlightMode->integer )
#endif
	{
		r *= r_dlightIntensity->value;
		g *= r_dlightIntensity->value;
		b *= r_dlightIntensity->value;
		intensity *= r_dlightScale->value;
	}
#endif

	if ( r_dlightSaturation->value != 1.0 )
	{
		float luminance = LUMA( r, g, b );
		r = LERP( luminance, r, r_dlightSaturation->value );
		g = LERP( luminance, g, r_dlightSaturation->value );
		b = LERP( luminance, b, r_dlightSaturation->value );
	}

	dl = &backEndData->dlights[ r_numdlights++ ];
	VectorCopy( start, dl->origin );
	VectorCopy( end, dl->origin2 );
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->additive = 0;
	dl->linear = qtrue;
}


/*
=====================
RE_AddWaterRipple

[QL] R19: something disturbed the water here.

Deliberately not stored in backEndData like the lights and the entities are.
Those are the frame's scene and are rebuilt from nothing every frame; a ripple
is added by an event that happens in one frame and has to keep spreading for a
second or two afterwards, while nothing is adding it. So it lives in tr, ages
out on its own, and the caller fires and forgets.

No water test here, and that is on purpose. cgame knows it hit something wet -
it already checked the contents to decide whether to play a splash sound - and
the renderer's water planes are the wrong thing to test against anyway: an
explosion a few units above the surface should still ripple it. The reflection
pass decides what, if anything, a given ripple touches.
=====================
*/
/*
=====================
R_LoadWaterProfile

[QL] R19: per-map water settings, from scripts/water.cfg.

Block format rather than one line per map, for the reason Quake's own shader
files use it: a map can set one field and inherit the rest, and a field can be
added later without every existing entry becoming malformed. Same reason a
missing key is silent and an unknown key is loud - the first is a map that does
not care, the second is a typo that would otherwise do nothing at all and say
nothing about it, which is the same silent-failure shape as a registered cvar
that nothing reads.

A `map` of `default` applies to everything not named, so a sensible baseline
does not have to be repeated. The named entry wins.

No file is not an error. Neither is a map that is not in it.
=====================
*/
void R_LoadWaterProfile( const char *mapName ) {
	union { char *c; void *v; } buffer;
	waterProfile_t defaults, named;
	const char *p;
	char *token;
	int len, applied;

	Com_Memset( &tr.waterProfile, 0, sizeof( tr.waterProfile ) );
	Com_Memset( &defaults, 0, sizeof( defaults ) );
	Com_Memset( &named, 0, sizeof( named ) );

	len = ri.FS_ReadFile( "scripts/water.cfg", &buffer.v );
	if ( len <= 0 || buffer.c == NULL ) {
		return;         // nothing to say about any map
	}

	p = buffer.c;

	while ( 1 ) {
		waterProfile_t block;
		char blockMap[ MAX_QPATH ];

		token = R_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			break;      // end of file
		}
		if ( Q_stricmp( token, "{" ) != 0 ) {
			ri.Printf( PRINT_WARNING, "water.cfg: expected '{', found '%s' - "
				"stopping here; everything after this point is ignored\n", token );
			break;
		}

		Com_Memset( &block, 0, sizeof( block ) );
		blockMap[0] = '\0';

		while ( 1 ) {
			token = R_ParseExt( &p, qtrue );
			if ( !token[0] || Q_stricmp( token, "}" ) == 0 ) {
				break;
			}

			if ( Q_stricmp( token, "map" ) == 0 ) {
				token = R_ParseExt( &p, qfalse );
				Q_strncpyz( blockMap, token, sizeof( blockMap ) );
			}
#define WATER_KEY( name, field, have ) \
			else if ( Q_stricmp( token, name ) == 0 ) { \
				token = R_ParseExt( &p, qfalse ); \
				block.field = atof( token ); \
				block.have = qtrue; \
			}
			WATER_KEY( "scale",     scale,     haveScale )
			WATER_KEY( "speed",     speed,     haveSpeed )
			WATER_KEY( "steepness", steepness, haveSteepness )
			WATER_KEY( "height",    height,    haveHeight )
			WATER_KEY( "strength",  strength,  haveStrength )
			/* [QL] R28: impacts, as opposed to the wind chop above */
			WATER_KEY( "ripplesize",   rippleSize,   haveRippleSize )
			WATER_KEY( "rippleheight", rippleHeight, haveRippleHeight )
			WATER_KEY( "ripplewaves",  rippleWaves,  haveRippleWaves )
			WATER_KEY( "ripplelife",   rippleLife,   haveRippleLife )
#undef WATER_KEY
			else {
				ri.Printf( PRINT_WARNING, "water.cfg: unknown key '%s' in block for '%s' - "
					"ignored\n", token, blockMap[0] ? blockMap : "(no map named yet)" );
				R_ParseExt( &p, qfalse );   // and its value
			}
		}

		if ( !blockMap[0] ) {
			ri.Printf( PRINT_WARNING, "water.cfg: a block with no 'map' - ignored\n" );
		} else if ( Q_stricmp( blockMap, "default" ) == 0 ) {
			defaults = block;
		} else if ( Q_stricmp( blockMap, mapName ) == 0 ) {
			named = block;
		}
	}

	ri.FS_FreeFile( buffer.v );

	/* the named entry over the default block, field by field */
	tr.waterProfile = defaults;
#define WATER_TAKE( field, have ) \
	if ( named.have ) { tr.waterProfile.field = named.field; tr.waterProfile.have = qtrue; }
	WATER_TAKE( scale,     haveScale )
	WATER_TAKE( speed,     haveSpeed )
	WATER_TAKE( steepness, haveSteepness )
	WATER_TAKE( height,    haveHeight )
	WATER_TAKE( strength,  haveStrength )
	WATER_TAKE( rippleSize,   haveRippleSize )
	WATER_TAKE( rippleHeight, haveRippleHeight )
	WATER_TAKE( rippleWaves,  haveRippleWaves )
	WATER_TAKE( rippleLife,   haveRippleLife )
#undef WATER_TAKE

	/*
	[QL] Counted from the profile rather than tracked as the keys are read,
	because the same field can be set by both blocks and must count once.
	*/
#define WATER_COUNT( p ) ( (p).haveScale + (p).haveSpeed + (p).haveSteepness + \
	(p).haveHeight + (p).haveStrength + (p).haveRippleSize + (p).haveRippleHeight + \
	(p).haveRippleWaves + (p).haveRippleLife )

	applied = WATER_COUNT( tr.waterProfile );

	if ( applied ) {
		ri.Printf( PRINT_ALL, "Water: %i setting(s) for %s from water.cfg%s\n",
			applied, mapName, WATER_COUNT( named ) ? "" : " (the default block)" );
	}
#undef WATER_COUNT
}


/*
=====================
R_WaterSetting

[QL] R19: the map's value, or the player's, and the rule is one comparison.

A map value applies only where the cvar is still at its shipped default. The
moment a player sets it themselves, theirs wins - on this map and every other -
and setting it back to the default hands control to the maps again.

Compared against resetString rather than tracked with a snapshot at map load on
purpose. There is no state to get out of step, nothing is ever written into a
cvar so nothing reaches the player's config, and the question "why is my setting
being ignored" has one answer that is true every time.
=====================
*/
float R_WaterSetting( const cvar_t *cv, qboolean haveMapValue, float mapValue ) {
	if ( haveMapValue && cv->resetString != NULL &&
	     Q_stricmp( cv->string, cv->resetString ) == 0 ) {
		return mapValue;
	}
	return cv->value;
}


void RE_AddWaterRipple( const vec3_t origin, float radius, float strength ) {
	waterRipple_t *rp;

	if ( !tr.registered || !tr.world ) {
		return;
	}
	if ( strength <= 0.0f || radius <= 0.0f ) {
		return;
	}

	/*
	Overwrite the oldest when full rather than dropping the newest. A ripple
	that has been spreading longest is the one closest to having faded out, and
	the new one is the event the player just caused and is looking at.
	*/
	rp = &tr.waterRipples[ tr.numWaterRipples % MAX_WATER_RIPPLES ];
	tr.numWaterRipples++;

	VectorCopy( origin, rp->origin );
	rp->radius = radius;
	rp->strength = strength;
	/*
	[QL] Unstamped. The backend puts the time on it, and that is not fussiness.

	tr.refdef.time is whatever the last scene set, and cgame renders 3D model
	icons for the HUD through their own refdef - so by the time an event fires
	this holds that refdef's clock, which is near zero, and not the world's.
	The line above at floatTime shows this tree already has to special-case
	such scenes.

	The result was every ripple arriving stamped at about zero against a world
	clock ten seconds in, and every one of them discarded as expired before it
	drew a single frame. The console said the events arrived and the water said
	nothing happened, and both were telling the truth.

	So the only clock used is backEnd.refdef.time, at both ends. A negative
	startTime means "not yet seen by the reflection pass"; it stamps it the
	first time it looks.
	*/
	rp->startTime = -1;

	/*
	[QL] Loud enough to find, quiet enough to live with.

	"I see no ripples" has two completely different causes - cgame never called,
	or it called and the reflection pass did nothing with it - and no way to
	tell them apart from the screen. One line settles it: if it appears, the
	event arrived and the fault is downstream.

	ONE line, though. This printed per ripple, and since `developer` defaults to
	1 in this tree - and now stays that way through the Alpha 2 candidates,
	which is a decision, not an oversight - a few seconds of shooting at water
	pushed everything else out of the scrollback. A diagnostic that buries the
	next diagnostic is a net loss, and the last three water bugs here were all
	found by reading console output.

	So: the first ripple after each world load prints and the rest are counted.
	That still answers the question the line exists for, with no cvar to set and
	no second run - which matters most for exactly the tester who cannot
	reproduce on demand. r_ssrDebug restores the per-ripple detail for anyone
	actually chasing placement, and the tail count means a silent console still
	distinguishes "none arrived" from "many arrived".
	*/
	r_rippleCount++;

	if ( r_ssrDebug->integer ) {
		ri.Printf( PRINT_DEVELOPER, "water ripple: %.0f %.0f %.0f, reach %.0f, strength %.1f\n",
			origin[0], origin[1], origin[2], radius, strength );
	} else if ( r_rippleCount == 1 ) {
		ri.Printf( PRINT_DEVELOPER, "water ripple: %.0f %.0f %.0f, reach %.0f, strength %.1f"
			" (further ripples counted, not printed - r_ssrDebug 1 for each one)\n",
			origin[0], origin[1], origin[2], radius, strength );
	}
}


/*
====================
R_ResetRippleDiag

[QL] Called from RE_LoadWorldMap. Without this the "first ripple" line is the
first of the session rather than the first of the map, so a map where cgame
never calls at all would look identical to one where it did - which is the
single distinction the line is for.
====================
*/
void R_ResetRippleDiag( void ) {
	if ( r_rippleCount > 1 ) {
		ri.Printf( PRINT_DEVELOPER, "water ripples last map: %i\n", r_rippleCount );
	}
	r_rippleCount = 0;
}



/*
=====================
RE_AddLightToScene

=====================
*/
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, intensity, r, g, b, qfalse );
}


/*
=====================
RE_AddAdditiveLightToScene

=====================
*/
void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, intensity, r, g, b, qtrue );
}


void *R_GetCommandBuffer( int bytes );

/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *fd ) {
#ifdef USE_VULKAN
	renderCommand_t	lastRenderCommand;
#endif
	viewParms_t		parms;
	int				startTime;

	if ( !tr.registered ) {
		return;
	}

	if ( r_norefresh->integer ) {
		return;
	}

	startTime = ri.Milliseconds();

	if (!tr.world && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

	Com_Memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for ( i = 0; i < MAX_MAP_AREA_BYTES/sizeof(int); i++ ) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}


	// derived info

	/*
	[QL] Where shader animation gets its clock - r_shaderTimeSource.

	Everything time-driven in a shader hangs off this one value: animMap frame
	selection, every tcMod (scroll, rotate, turb, stretch), rgbGen/alphaGen wave
	and deformVertexes all evaluate against tess.shaderTime, which is
	refdef.floatTime minus the shader's own offset.

	  0  scene time (default)  refdef.time, which cgame sets to cg.time. Correct
	                           by construction: it follows demo playback and
	                           timescale, and shader animation stays in step with
	                           game events. But cg.time is snapshot-interpolated,
	                           so if it advances unevenly - which it does when the
	                           server is hitching, and is more visible the more
	                           frames you draw between snapshots - the animation
	                           inherits that unevenness.

	  1  real time             the engine millisecond clock, independent of
	                           snapshots entirely. World animation stays smooth
	                           regardless of what the server is doing. The cost is
	                           that it ignores timescale and does not pause or
	                           rewind with a demo.

	This exists because animation speed was reported as varying with framerate and
	every path in the renderer reads from a clock rather than a frame counter - so
	the coupling, if it is real, is in what feeds refdef.time rather than in how it
	is used. Switching the source settles that: if 1 is smooth and 0 is not, the
	problem is cg.time, not the shaders.
	*/
	if ( r_shaderTimeSource->integer ) {
		tr.refdef.floatTime = (double)ri.Milliseconds() * 0.001;
	} else {
		tr.refdef.floatTime = (double)tr.refdef.time * 0.001; // -EC-: cast to double
	}

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

#ifdef USE_PMLIGHT
	tr.refdef.numLitSurfs = r_firstSceneLitSurf;
	tr.refdef.litSurfs = backEndData->litSurfs;
#endif

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	// turn off dynamic lighting globally by clearing all the
	// dlights if it needs to be disabled
	if ( r_dynamiclight->integer == 0 || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		tr.refdef.num_dlights = 0;
	}

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;

	// setup view parms for the initial view
	//
	// set up viewport
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
	Com_Memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - ( tr.refdef.y + tr.refdef.height );
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;

	parms.scissorX = parms.viewportX;
	parms.scissorY = parms.viewportY;
	parms.scissorWidth = parms.viewportWidth;
	parms.scissorHeight = parms.viewportHeight;

	parms.portalView = PV_NONE;

#ifdef USE_PMLIGHT
	parms.dlights = tr.refdef.dlights;
	parms.num_dlights = tr.refdef.num_dlights;
#endif

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;
	
	parms.stereoFrame = tr.refdef.stereoFrame;

	VectorCopy( fd->vieworg, parms.or.origin );
	VectorCopy( fd->viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd->viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd->viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd->vieworg, parms.pvsOrigin );

#ifdef USE_VULKAN
	lastRenderCommand = tr.lastRenderCommand;
	tr.drawSurfCmd = NULL;
	tr.numDrawSurfCmds = 0;
#endif

	R_RenderView( &parms );

#ifdef USE_VULKAN
	if ( tr.needScreenMap )
	{
		if ( lastRenderCommand == RC_DRAW_BUFFER )
		{
			// duplicate all views, including portals
			drawSurfsCommand_t *cmd, *src = NULL;
			int i;

			for ( i = 0; i < tr.numDrawSurfCmds; i++ )
			{
				cmd = R_GetCommandBuffer( sizeof( *cmd ) );
				if ( cmd )
				{
					src = tr.drawSurfCmd + i;
					*cmd = *src;
				}
				else
				{
					break;
				}
			}

			if ( src )
			{
				// first drawsurface
				tr.drawSurfCmd[0].refdef.needScreenMap = qtrue;
				// last drawsurface
				src->refdef.switchRenderPass = qtrue;
			}
		}

		tr.needScreenMap = 0;
	}
#endif

	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
#ifdef USE_PMLIGHT
	r_firstSceneLitSurf = tr.refdef.numLitSurfs;
#endif

	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}
