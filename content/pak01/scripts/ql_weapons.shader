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
// The lightning bolt, style 6.
//
// A sixth style rather than a replacement for any of the five in pak00, so
// nothing about the gun changes until cg_lightningStyle 6 asks for it.
//
// What it is for: styles 1 to 4 are additive, and additive light has nowhere to
// go on a surface that is already white. On japanesecastles the shoji panes are
// near enough to 1.0 that the bolt disappears over them entirely while staying
// bright over the dark wood beside them - which reads as the panes being drawn
// on top of the beam, and is not. Style 5 does not do it, which is what proved
// the cause: all five bolts sort at 9.00 and are depth-tested identically, so
// the only thing that can differ between them is how they blend.
//
// Getting a bolt to read on a white background needs the result to land below
// white, and that rules out the obvious blends. Additive, screen
// (GL_ONE GL_ONE_MINUS_SRC_COLOR) and soft-add (GL_ONE_MINUS_DST_COLOR GL_ONE)
// all evaluate to exactly 1.0 against a white destination, whatever the source
// is. The usual escape - alpha blending with the bolt's own shape as the alpha
// - is not available either: every lightning texture in the pak is a .jpg and
// has no alpha channel, and GL_SRC_COLOR is not a legal source factor here.
//
// So it is done in two stages. The first multiplies the destination by the
// inverse of the bolt, which carves the bolt's shape out of whatever is behind
// it and is the step that makes room on a bright surface. The second adds the
// bolt back tinted and below full strength, so the sum stays under white and
// the colour survives instead of washing out.
//
// Against black the first stage does nothing and the second is an ordinary
// additive bolt. Against white the first stage does the work. Both ends of the
// range read, which is the whole point.
//
// RB_SurfaceLightningBolt draws four quads rotated about the beam axis, so
// every fragment near the axis goes through this four times: the darkening
// compounds and the additive term saturates, which should put a white-hot core
// inside a blue bolt. That is the intent, and it is the part most worth judging
// on screen rather than from the arithmetic.
lightningBolt6
{
	cull none
	nopicmip
	{
		map gfx/misc/lightning2.jpg
		blendFunc GL_ZERO GL_ONE_MINUS_SRC_COLOR
		rgbGen identity
	}
	{
		map gfx/misc/lightning2.jpg
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.45 0.70 1.00 )
	}
}
