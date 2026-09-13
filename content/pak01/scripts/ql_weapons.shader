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
// The shoji screens.
//
// These have no shader in pak00 - they are plain lightmapped surfaces, which
// the renderer generates as two stages: the lightmap, then the diffuse
// multiplied over it. What is below is exactly that generated pair, with one
// change: the diffuse is scaled down so the result cannot reach 255.
//
// Why it needs to. The panes are near-white paper and the map lights them hard,
// and R_ColorShiftLightingBytes normalises an overflowing lightmap texel by its
// brightest channel rather than clipping - so a hot texel comes back as
// (255,255,255) and stays there. Pinned at white there is no headroom left, and
// an additive effect drawn over them adds nothing at all. That is what ate the
// lightning bolt, and no amount of work on the bolt could have fixed it: four
// of the five bolt styles are additive and the fifth only differs because it
// blends.
//
// The alternative was the overbright pipeline itself, which is where this went
// first. r_fbo 1 does fix it - it stops textures being gamma-baked at upload and
// moves overbright to shade time where nothing clamps - but that multiply then
// lands on the whole frame instead of only the lightmap, and the rest of the
// map blows out. Correcting one surface by re-exposing every surface is the
// wrong trade.
//
// 0.75 is a dial, not a derivation: it leaves about a quarter of the range free,
// which is enough for an additive bolt to read, and costs a quarter of the glow.
// These panes are the lit paper walls of the whole building seen from outside,
// so the number is a look decision and belongs to whoever is looking at it.
//
// Both variants, because the map uses both and half a fix would show as a seam.
textures/gothic_trim/window_a1
{
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/gothic_trim/window_a1.jpg
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.75 0.75 0.75 )
	}
}

textures/gothic_trim/window_a2
{
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/gothic_trim/window_a2.jpg
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.75 0.75 0.75 )
	}
}
