// [QL] Freeze Tag ice shell.
//
// A frozen player is drawn three times: the model normally, then a coat, then an
// animated overlay (CG_AddRefEntityWithPowerups). Coat and overlay are separate
// shaders because one shader gets one deformVertexes, and they stand off the
// model by different amounts.
//
// What each part is for, and what went wrong getting there:
//
// The coat occludes rather than adds. Two GL_ONE GL_ONE stages of a bright
// environment map add roughly twice the map's brightness on top of the model,
// which saturates to white and erases the player - Quake Live's stays completely
// readable. So it is alpha blended, and alphaGen lightingSpecular puts the
// opacity where the light falls, which is what reads as facets.
//
// It stands off the model. Quake Live's ice hovers rather than clinging, which
// is what makes it a block of ice with someone inside instead of a shiny skin. A
// second pass of the same mesh is skin-tight by definition, so the geometry is
// pushed along the vertex normals - deformVertexes is the only thing in idTech3
// that moves geometry from a shader.
//
// Nothing animates. Ice does not breathe, which takes care when the deform is
// spelled as a wave: amplitude 0 with frequency 0 is the constant case, and
// RB_CalcDeformVertexes has a frequency == 0 branch that evaluates the waveform
// once and pushes every vertex by that fixed amount. Same reason every rgbGen
// here is const, and there is no tcMod anywhere.
//
// Everything here is faint on purpose. Each shader is drawn once per model part
// - legs, torso and head - so it accumulates wherever those overlap, and a value
// that looks mild on a single surface blows out across a whole player. That is
// what happened to the first halo.
//
// The coat shrinks as the thaw progresses. A shader cannot read game state, but
// the server already publishes the progress: Freeze_ClientThawCheck buckets
// ps.thawtime into three by thirds and writes it into the low bits of generic1,
// which BG_PlayerStateToEntityState copies into the entity state and msg.c
// networks. So each style ships three coats at decreasing standoff and cgame
// picks by generic1 & 3 - the ice visibly closes in on the player as a teammate
// works on them, and vanishes as they come free.
//
// Two cvars pick the combination: cg_freezeShellStyle chooses the coat,
// cg_freezeShellEffect chooses the animated overlay (0 turns it off).

// ===========================================================================
// Coats - cg_freezeShellStyle picks the set, generic1 & 3 picks the size
// ===========================================================================
//
// Three per style, thickest to thinnest, matching the three thaw buckets the
// server publishes. Tier 1 is a fresh statue; tier 3 is about to come free.
//
// $whiteimage is the renderer's built-in 1x1 white and is the only way to get
// a white coat: rgbGen scales what a texture already has, so icemap's blue
// cannot be desaturated out of it in the fixed pipeline.

// style 1 - blue, close (2, 1.2, 0.5 units)
powerups/freezecoat1_1
{
	deformVertexes wave 100 sin 2 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

powerups/freezecoat1_2
{
	deformVertexes wave 100 sin 1.2 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

powerups/freezecoat1_3
{
	deformVertexes wave 100 sin 0.5 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

// style 2 - white, close (2, 1.2, 0.5 units)
powerups/freezecoat2_1
{
	deformVertexes wave 100 sin 2 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

powerups/freezecoat2_2
{
	deformVertexes wave 100 sin 1.2 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

powerups/freezecoat2_3
{
	deformVertexes wave 100 sin 0.5 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

// style 3 - blue, wide (5, 3, 1.2 units)
powerups/freezecoat3_1
{
	deformVertexes wave 100 sin 5 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

powerups/freezecoat3_2
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

powerups/freezecoat3_3
{
	deformVertexes wave 100 sin 1.2 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.14 0.20 0.30 )
		tcGen environment
	}
}

// style 4 - white, wide (5, 3, 1.2 units)
powerups/freezecoat4_1
{
	deformVertexes wave 100 sin 5 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

powerups/freezecoat4_2
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

powerups/freezecoat4_3
{
	deformVertexes wave 100 sin 1.2 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.08 0.08 0.08 )
	}
}

// ===========================================================================
// Overlays - cg_freezeShellEffect
// ===========================================================================
//
// A wider, fainter halo turned out to be the wrong idea - a glow around the
// silhouette reads as a powerup, not as ice. These are the other approach: an
// animated stage sitting just outside the coat, so the surface has movement in
// it while the shell itself stays still. The geometry is static in all of them
// (amplitude 0, frequency 0); only the texture moves.
//
// 3 units of standoff, one outside the close coats, so it layers over style 1
// and 2. With the wide coats (3 and 4, at 5 units) it sits inside - both are
// alpha-blended and neither writes depth, so it still shows, just dimmer.
//
// Faint by design: this is drawn once per model part - legs, torso, head - and
// accumulates wherever they overlap, which is what made the old halo blow out.

// 1: slow swirl. The environment map rotating in place, so the reflection
// crawls over the shell as if the ice were catching light from a turning
// source. Cheapest of the four and the least likely to be distracting.
powerups/freezeglow1
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.16 0.20 0.26 )
		tcGen environment
		tcMod rotate 8
	}
}

// 2: turbulent shimmer. tcMod turb distorts the texture coordinates on a sine,
// which is what Quake 3 uses for heat haze and energy surfaces - here it makes
// the reflection ripple rather than slide.
powerups/freezeglow2
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.16 0.20 0.26 )
		tcGen environment
		tcMod turb 0 0.14 0 0.5
	}
}

// 3: animated frames. A real two-frame animMap between Quake Live's own blue
// environment maps, which are a matched pair and cycle without a visible seam.
// animMap's first argument is frames per second - 2 is a slow flicker, not a
// strobe.
powerups/freezeglow3
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		animMap 2 textures/effects/envmapblue.jpg textures/effects/envmapblue2.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.18 0.22 0.30 )
		tcGen environment
	}
}

// 4: crawling frost. tcGen base uses the player model's own UV layout rather
// than a view-derived environment map, so this one is fixed to the body and
// scrolls across it - frost spreading over the model instead of a reflection
// moving across a shell.
powerups/freezeglow4
{
	deformVertexes wave 100 sin 3 0 0 0
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.20 0.24 0.32 )
		tcMod scroll 0.04 0.015
	}
}
