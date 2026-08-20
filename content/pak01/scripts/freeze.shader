// [QL] Freeze Tag ice shell.
//
// A frozen player is drawn three times: the model normally, then a coat, then a
// halo (CG_AddRefEntityWithPowerups). The coat and halo are separate shaders
// because one shader gets one deformVertexes, and the halo has to stand further
// off the model than the coat does.
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
// The halo is faint on purpose. It is drawn once per model part - legs, torso
// and head - so it accumulates wherever those overlap, and a value that looks
// mild on a single surface blows out across a whole player.
//
// Two cvars pick the combination: cg_freezeShellStyle chooses the coat,
// cg_freezeShellEffect chooses the halo (0 turns it off).

// ===========================================================================
// Coats - cg_freezeShellStyle
// ===========================================================================

// 1: blue, close (default). Quake Live's own ice environment map, 2 units off.
powerups/freezecoat1
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

// 2: white, close. $whiteimage is the renderer's built-in 1x1 white - the only
// way to get a white coat, since rgbGen scales what a texture already has and
// icemap's blue cannot be desaturated out of it in the fixed pipeline.
powerups/freezecoat2
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

// 3: blue, wide. Same as 1 at 5 units.
powerups/freezecoat3
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

// 4: white, wide. Same as 2 at 5 units.
powerups/freezecoat4
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

// ===========================================================================
// Halos - cg_freezeShellEffect
// ===========================================================================
//
// GL_SRC_ALPHA GL_ONE is additive weighted by alpha, so lightingSpecular
// concentrates the halo where light catches the hull and lets it fall away
// elsewhere. A flat additive shell at this size reads as a pale silhouette
// rather than a glow.

// 1: white, subtle (default). 7 units off.
powerups/freezeglow1
{
	deformVertexes wave 100 sin 7 0 0 0
	cull none
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.12 0.12 0.12 )
		alphaGen lightingSpecular
	}
}

// 2: white, stronger. Same hull, roughly twice as bright.
powerups/freezeglow2
{
	deformVertexes wave 100 sin 7 0 0 0
	cull none
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.26 0.26 0.26 )
		alphaGen lightingSpecular
	}
}

// 3: cold blue rather than white, subtle.
powerups/freezeglow3
{
	deformVertexes wave 100 sin 7 0 0 0
	cull none
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.08 0.13 0.22 )
		alphaGen lightingSpecular
	}
}
