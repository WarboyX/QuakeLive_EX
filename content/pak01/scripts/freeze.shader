// [QL] Freeze Tag ice shell.
//
// The model is drawn normally first and then a second time with a customShader,
// exactly the way the quad shell works (CG_AddRefEntityWithPowerups). So this
// shader is a coat over a player who is still fully drawn underneath - and that
// is the part the first attempt got wrong. Two GL_ONE GL_ONE stages of a bright
// environment map add up to roughly twice the map's brightness on top of the
// model, which saturates to white and erases the player: no team colour, no
// skin, just a pale silhouette. Quake Live's own frozen player stays completely
// readable underneath, ice over armour rather than instead of it.
//
// The image is Quake Live's own ice environment map, referenced by name. Only
// the name lives here - no asset content is redistributed, same rule as
// docs/pak-manifest.txt.
//
// Three variants, selected by cg_freezeShell, because the right answer is a
// judgement about how it looks and that is faster to settle by looking at all
// three in game than by rebuilding a pak between guesses.

// cg_freezeShell 1 - glass coat (default)
//
// Alpha-blended rather than additive, so the shell occludes rather than adds:
// the model underneath keeps its own colour and the ice reads as a layer over
// it. alphaGen lightingSpecular puts the opacity where the light is, which is
// what gives ice its facets - bright where a surface catches the light, nearly
// clear where it does not - instead of an even film. The faint additive stage
// on top is the sheen, at a quarter strength, and rotates slowly so a statue
// is not completely static.
powerups/freezeshell1
{
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
		rgbGen const ( 0.20 0.28 0.38 )
		tcGen environment
		tcMod rotate 6
	}
}

// cg_freezeShell 2 - additive sheen
//
// What the first attempt was trying to be: one additive environment stage, like
// powerups/quad, at a strength that leaves the model visible. Brighter and more
// obviously "glowing" than variant 1, and it will wash out a pale skin more.
powerups/freezeshell2
{
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen const ( 0.35 0.45 0.60 )
		tcGen environment
	}
}

// cg_freezeShell 3 - flat frozen sprite
//
// Quake Live's own sprites/frozen, blended rather than added. The plainest of
// the three and the least likely to fight a map's lighting.
powerups/freezeshell3
{
	{
		map sprites/frozen
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen const 0.65
		tcGen environment
	}
}
