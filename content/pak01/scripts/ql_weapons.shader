// ---------------------------------------------------------------------------
// [QL] Weapon shaders replaced because the ones in pak00 ask for files that
// pak00 does not contain.
//
// A shader here overrides the one of the same name in pak00: shader text is
// concatenated highest-priority-search-path last, and the lookup returns the
// last definition it finds. pak01 sorts above pak00, so these win.
//
// Nothing in this file is a Quake Live asset. It references textures that are
// already in the pak by name and ships no art of its own, which is the same
// footing as docs/pak-manifest.txt.
// ---------------------------------------------------------------------------

// The nailgun.
//
// pak00's version of this shader is the Team Arena original, and its first
// stage is "models/weapons/nailgun/nailgun_env.tga" - an environment map that
// did not survive Quake Live's conversion to .png. There is no nailgun_env in
// the pak under any extension; in fact there are only three *_env files in the
// whole pak and all three are map textures.
//
// The renderer used to discard a whole shader over one missing image, so the
// nailgun drew as the black and white grid. It now drops just that stage - and
// that is what made the gun see-through, which is the state this file fixes.
//
// The diffuse stage is "blendFunc blend". It was written to composite over the
// environment stage, so with that stage gone it blends against whatever is
// behind the model instead: the world shows through the gun wherever
// nailgun.png's alpha is below 1. The transparency is therefore also the proof
// that the texture has an alpha channel and is painted expecting chrome
// underneath it - the sheen is not decoration on this model, it is the half of
// the material the artist left room for.
//
// So the stage is restored with an environment map that is already in the pak,
// in the arrangement id's own weapon shaders use: environment underneath,
// diffuse blended over the top, so metal reads through where the diffuse is
// transparent and the painted camo stays painted everywhere else. The rocket
// launcher and railgun are shiny for the same reason and by the same means.
//
// Which environment map matters, and the first attempt got it wrong. tinfx is
// bright tin, and under a diffuse this transparent it lifted the entire gun to
// a uniform pale grey. The weapon is not pale: it is dark weathered gunmetal
// with orange hazard panels, and only the barrel tube and the strut rails are
// polished. envmapdim is the dim variant and leaves the body dark, which is the
// half of the reference tinfx was erasing.
//
// There is no envmapnail to be had. The per-weapon set in textures/effects -
// envmapmach, envmaprail, envmaproc, envmapligh, envmapplas, envmapbfg - covers
// the Quake 3 weapons only; the Team Arena three carried their own *_env.tga
// beside their models, which is precisely what Quake Live's conversion dropped.
models/weapons/nailgun/nailgun
{
	{
		map textures/effects/envmapdim.jpg
		rgbGen lightingDiffuse
		tcGen environment
	}
	{
		map models/weapons/nailgun/nailgun.png
		blendFunc blend
		rgbGen lightingDiffuse
	}
}

// ---------------------------------------------------------------------------
// The shoji screens, as frosted glass.
//
// These have no shader in pak00 - they are plain lightmapped surfaces, which
// the renderer generates as a lightmap stage and a diffuse multiplied over it.
// The map lights near-white paper hard, and R_ColorShiftLightingBytes
// normalises an overflowing lightmap texel by its brightest channel rather than
// clipping, so a hot texel comes back (255,255,255) and stays there. Pinned at
// white there is nothing left above, and an additive effect drawn over them adds
// nothing at all. That is what ate the lightning bolt - four of the five bolt
// styles are additive - and no work on the bolt could have fixed it.
//
// Dimming the paper to 0.75 proved the diagnosis: the panes stopped being
// perfectly white and the lightmap variation underneath them became visible for
// the first time, having been hidden by the clipping all along. But it also
// showed that dim paper is not what these want to be. What is behind them is
// daylight, not a lamp in the room.
//
// So they are glass now rather than paper, and the shape of the shader is the
// point rather than the numbers in it:
//
//   No lightmap. Backlit glass is lit from the far side, so shading it with the
//   near side's lightmap is what produced the blotches - and dropping it makes
//   the panels a uniform colour, which is what they read as from outside the
//   building and what they should read as from inside.
//
//   A cool, dim base instead of hot white. This is the headroom: a surface that
//   sits near 0.5 has half the range free, so anything additive drawn over it -
//   a bolt, a muzzle flash, an explosion - has somewhere to go. It is also just
//   what glass looks like next to lit wood.
//
//   An environment map over the top, additive and faint. That is the whole
//   glass read: a sheen that slides across the panel as you move, which a flat
//   texture cannot do at any brightness. envmapdimb is the dim neutral one -
//   the coloured variants are for weapons and would tint the whole wall.
//
// Both variants, because the map uses both and half of it would show as a seam.
// gothic_trim is a stock Quake 3 set, so any other map using window_a1/a2 gets
// this too.
// [QL] The shoji panes are glass, not lit paper.
//
// surfaceparm nolightmap is not decoration here. The BSP carries lightmap data
// for these faces, and a shader with no lightmap stage to consume it logs
// "has lightmap but no lightmap stage!" and then takes whatever the engine
// decides to do about it - which was the panes picking up shadowing and coming
// out unevenly lit, the thing that made them look wrong in the first place.
// Declaring it says the surface does not want one, so the colour below is the
// colour, everywhere on the pane.
textures/gothic_trim/window_a1
{
	surfaceparm nolightmap
	{
		map textures/gothic_trim/window_a1.jpg
		rgbGen const ( 0.50 0.54 0.60 )
	}
	{
		map textures/effects/envmapdimb.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.30 0.30 0.34 )
	}
}

textures/gothic_trim/window_a2
{
	surfaceparm nolightmap
	{
		map textures/gothic_trim/window_a2.jpg
		rgbGen const ( 0.50 0.54 0.60 )
	}
	{
		map textures/effects/envmapdimb.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.30 0.30 0.34 )
	}
}
