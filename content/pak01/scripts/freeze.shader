// [QL] Freeze Tag ice shell.
//
// The model is drawn normally first and then a second time with a customShader,
// the same way the quad shell works (CG_AddRefEntityWithPowerups). So this
// shader is a coat over a player who is still fully drawn underneath.
//
// Two things had to be got right and the first attempts missed both.
//
// Brightness: two GL_ONE GL_ONE stages of a bright environment map add roughly
// twice the map's brightness on top of the model, which saturates to white and
// erases the player. Quake Live's frozen player stays completely readable - blue
// armour and model detail visible through the ice - so the shell has to occlude
// rather than add, which means alpha blending, not additive.
//
// Standoff: Quake Live's ice *hovers* around the player rather than clinging to
// the model surface, which is what makes it read as a block of ice with someone
// inside rather than a shiny skin. A second pass of the same mesh is skin-tight
// by definition, so the shell has to be pushed outward, and the only way a
// shader can move geometry in idTech3 is deformVertexes.
//
//   deformVertexes wave <div> <func> <base> <amplitude> <phase> <freq>
//
// moves each vertex along its own normal. The *base* is the constant push - that
// is the standoff - and the amplitude is a slow breathing on top of it, which
// keeps a statue from being completely inert. div spreads the phase across the
// model by vertex position, so the surface flexes slightly rather than
// inflating as one rigid ball.
//
// The image is Quake Live's own ice environment map, referenced by name. Only
// the name lives here - no asset content is redistributed, same rule as
// docs/pak-manifest.txt.
//
// Three variants, selected by cg_freezeShell, sampling the two axes that are
// still open: how far the shell stands off, and whether it reads as glass or as
// glow. Faster to settle by looking at all three in game than by rebuilding a
// pak between guesses.

// cg_freezeShell 1 - glass, close hover (default)
//
// 4 units of standoff, breathing +/-1. Alpha-blended so the shell occludes
// rather than adds; alphaGen lightingSpecular puts the opacity where the light
// falls, which is what gives ice facets instead of an even film.
powerups/freezeshell1
{
	deformVertexes wave 100 sin 4 1 0 0.3
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
		rgbGen const ( 0.18 0.26 0.36 )
		tcGen environment
		tcMod rotate 6
	}
}

// cg_freezeShell 2 - glass, wide hover
//
// Same shell at 9 units, for when 4 still reads as painted on. This is the one
// to try if variant 1 is the right *look* but not far enough off the model.
powerups/freezeshell2
{
	deformVertexes wave 100 sin 9 2 0 0.25
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
		rgbGen const ( 0.18 0.26 0.36 )
		tcGen environment
		tcMod rotate 6
	}
}

// cg_freezeShell 3 - glow, wide hover
//
// The quad-shell treatment at a distance: one dimmed additive environment stage
// on a 9-unit standoff. Brighter and more obviously an effect than 1 or 2, and
// it will wash out a pale skin more - this is the "like quad damage" reading of
// the request rather than the "block of ice" one.
powerups/freezeshell3
{
	deformVertexes wave 100 sin 9 2 0 0.25
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.35 0.45 0.60 )
		tcGen environment
	}
}
