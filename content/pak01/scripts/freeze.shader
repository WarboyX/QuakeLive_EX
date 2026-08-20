// [QL] Freeze Tag ice shell.
//
// The model is drawn normally first and then again with a customShader, the way
// the quad shell works (CG_AddRefEntityWithPowerups). So these are coats over a
// player who is still fully drawn underneath.
//
// Four things had to be right and each attempt found one of them.
//
// Brightness: two GL_ONE GL_ONE stages of a bright environment map add roughly
// twice the map's brightness on top of the model, which saturates to white and
// erases the player. Quake Live's stays completely readable, so the shell has to
// occlude rather than add - alpha blending, not additive.
//
// Standoff: Quake Live's ice hovers around the player rather than clinging to
// the model, which is what makes it read as a block of ice with someone inside.
// A second pass of the same mesh is skin-tight by definition, so the geometry is
// pushed out along the vertex normals - deformVertexes is the only thing in
// idTech3 that moves geometry from a shader:
//
//   deformVertexes wave <div> <func> <base> <amplitude> <phase> <freq>
//
// Colour: white, not blue. textures/effects/icemap.jpg carries its own blue, and
// rgbGen only scales what the texture already has - there is no way to desaturate
// a texture in the fixed pipeline. So the coat is built on $whiteimage, the
// renderer's built-in 1x1 white, and the shape comes from alphaGen
// lightingSpecular instead of from the map: opacity follows the light, which is
// what reads as facets. The blue map is dropped rather than tinted down.
//
// Still: ice does not breathe. Everything here is static, which takes some care
// because the deform is spelled as a wave. Amplitude 0 and frequency 0 is the
// constant case - RB_CalcDeformVertexes has a branch for frequency == 0 that
// evaluates the waveform once and pushes every vertex by that fixed amount - so
// the shell holds its shape. Same reason the sheen is rgbGen const rather than
// rgbGen wave, and there is no tcMod anywhere.
//
// The glow is a second, wider shell drawn as its own pass (see
// CG_AddRefEntityWithPowerups) - one shader gets one deformVertexes, so a halo
// standing further off the model than the coat has to be a separate shader.

// ---------------------------------------------------------------------------
// The coat. cg_freezeShell picks the standoff.
// ---------------------------------------------------------------------------

// cg_freezeShell 1 - close hover (default), 4 units
powerups/freezeshell1
{
	deformVertexes wave 100 sin 4 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.10 0.10 0.10 )
	}
}

// cg_freezeShell 2 - wide hover, 9 units
powerups/freezeshell2
{
	deformVertexes wave 100 sin 9 0 0 0
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen lightingSpecular
	}
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.10 0.10 0.10 )
	}
}

// cg_freezeShell 3 - close hover with the ice environment map kept
//
// The blue one, for comparison: same 4-unit coat but textured with Quake Live's
// icemap rather than flat white. Keeps the map's crystal pattern, and its colour
// along with it.
powerups/freezeshell3
{
	deformVertexes wave 100 sin 4 0 0 0
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
		rgbGen const ( 0.18 0.26 0.38 )
		tcGen environment
	}
}

// ---------------------------------------------------------------------------
// The glow - a wider, fainter white shell over the coat.
// ---------------------------------------------------------------------------
//
// GL_SRC_ALPHA GL_ONE is additive weighted by alpha, so lightingSpecular
// concentrates it where light catches the hull and lets it fall away elsewhere.
// A flat additive shell this size would read as a pale silhouette rather than a
// glow. Deliberately faint: the request was a minor glow, and this is drawn
// three times per player (legs, torso, head), so it accumulates where the parts
// overlap.
powerups/freezeglow
{
	deformVertexes wave 100 sin 14 0 0 0
	cull none
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.55 0.55 0.55 )
		alphaGen lightingSpecular
	}
}
