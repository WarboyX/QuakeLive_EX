// [QL] E104. Deploying the R20/R27 surface work into Quake Live's OWN maps,
// without touching a map.
//
// WHY THIS FILE EXISTS
//
// The renderer work so far has only ever been seen on maps we compiled
// ourselves. The question was whether any of it reaches a stock Quake Live map,
// and the answer splits three ways:
//
//   R27 natural textures   YES, anywhere. Hex-tiling is a texture-space
//                          operation. It needs no map data at all, only a
//                          shader that asks for it.
//
//   R20 derived normals    YES, under a DYNAMIC light. The normal map is
//                          derived from the diffuse at load time, so nothing
//                          has to ship with the map. A rocket or a lightning
//                          beam lights it; the baked lightmap cannot.
//
//   R25 static bump        NO. It reads a deluxemap, which records the dominant
//                          light direction per lightmap texel, and only q3map2
//                          -deluxe writes one. Quake Live's maps carry none and
//                          never will. Recompiling them was tried and is
//                          recorded as not recommended (R23 in TRACKER.md).
//
// So two of the three deploy into an existing map with nothing but this file.
//
// HOW THE OVERRIDE WORKS, because it is worth being sure rather than hopeful.
// ScanAndLoadShaderFiles concatenates every scripts/*.shader it finds, then
// builds a hash table filling each bucket BACKWARDS
// (shaderTextHashTable[hash][--size]), and FindShaderInShaderText returns entry
// 0. So the LAST definition in the concatenated text wins - and the text is
// appended in reverse list order, which puts the higher-priority pak last.
// pak01 beats pak00. Checked in tr_shader.c, not assumed.
//
// WHAT THIS DOES NOT DO. It ships no art. Every one of these names is Quake
// Live's own texture, loaded out of Quake Live's own pak; all that is added is
// the keyword asking the renderer to treat it differently. Nothing here would
// work without a Quake Live install, and nothing here redistributes any part of
// one.
//
// The stage bodies below are transcribed from Quake Live's own scripts so the
// surface keeps its blend mode, its lightmap ordering and its q3map2 hints. Get
// that wrong and the override does not "fail" - it draws the surface wrongly,
// which is worse than not overriding it. Compare against the real file before
// adding a name here.
//
// TO SEE IT: /devmap any map using these materials, then
//     r_qlNaturalTextures 1     (the default - these shaders ask for it)
//     r_qlNormalMaps 1          (the default - R20 derives the normals)
//     vid_restart
// and look at a large expanse of cliff. The repeat is what should be gone.

// -----------------------------------------------------------------------------
// textures/stone/rockcliff_01 and _02
//
// Quake Live's definition is a lightmap stage followed by the diffuse with
// "blendfunc filter", plus q3map2_shadeangle 60 - reproduced exactly. The only
// addition is qlNaturalTexture on the diffuse stage.
//
// Cliff faces are the best possible candidate for hex-tiling: they are
// stochastic, they cover large areas, and the eye reads their repeat
// immediately. They are also the worst possible candidate for anything that
// assumes structure, which is why these and not, say, a trim or a sign.
//
// q3map_shadeangle is a compile-time hint and does nothing at run time. It is
// kept because a shader that differs from the original in ways nobody intended
// is how an override quietly becomes a bug.
// -----------------------------------------------------------------------------

textures/stone/rockcliff_01
{
	q3map_shadeangle 60
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/stone/rockcliff_01.tga
		blendfunc filter
		qlNaturalTexture
	}
}

textures/stone/rockcliff_02
{
	q3map_shadeangle 60
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/stone/rockcliff_02.tga
		blendfunc filter
		qlNaturalTexture
	}
}
