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
// So the stage is restored with textures/effects/tinfx, the environment map
// id's own weapon shaders use for exactly this - the rocket launcher and
// railgun are shiny for the same reason and by the same means. The arrangement
// below is theirs too: environment underneath, diffuse blended over the top, so
// the chrome reads through where the diffuse is transparent and the painted
// camo stays painted everywhere else.
models/weapons/nailgun/nailgun
{
	{
		map textures/effects/tinfx.jpg
		rgbGen lightingDiffuse
		tcGen environment
	}
	{
		map models/weapons/nailgun/nailgun.png
		blendFunc blend
		rgbGen lightingDiffuse
	}
}
