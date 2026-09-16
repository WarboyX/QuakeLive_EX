// [QL] Shaders for content/testmaps/qltest_light.map. R20 Stage A test only.
//
// These go in the TEST pk3 alongside the .bsp, not in pak01. They exist to
// exercise two paths in the derivation and have no business shipping:
//
//   cd baseq3 && zip -r qltest.pk3 maps/qltest_light.bsp scripts/qltest.shader
//
// Both reference textures/qltest/tangent, which is OURS - generated, committed
// under content/testmaps/textures/ and shipped in the test pk3. Nothing in this
// map depends on pak00.
//
// That is not only a licensing convenience. q3map2 converts the .map's texture
// axes into UVs using the image's real pixel dimensions, so compiling against a
// texture it cannot find silently produces the wrong UV scale: it falls back to
// a 64x64 default and the pattern comes out four times too large to read, with
// nothing but a WARNING line to say so. Shipping the texture makes the compile
// reproducible on a machine that has no Quake Live install.
//
// The pattern is a sawtooth - luminance ramps over 16 texels then drops off a
// cliff, horizontally in the top half and vertically in the bottom. A ramp is a
// constant luminance slope, so it derives to a constant tilt and lights as a
// flat band, and WHICH band is the bright one is exactly the sign of the
// tangent. A photograph of brick does not answer that question; this does.

// Panel 5. The opt-out. Diffuse stage first, so this is the lighting stage and
// the keyword lands on it. Must look exactly like panel 0 does with
// r_qlNormalMaps 0 - if it bumps, qlNoPerturb is not being read.
textures/qltest/noperturb
{
	qer_editorimage textures/qltest/tangent
	{
		map textures/qltest/tangent
		rgbGen identity
		qlNoPerturb
	}
	{
		map $lightmap
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// Panel 6. Lightmap stage FIRST, which is the ordering that collapses into a
// multitexture stage whose lighting bundle is bundle 1, not bundle 0.
//
// This is the regression test for the defect that made the derivation move out
// of ParseStage(): a version keyed on bundle[0] derives this panel's normal map
// from the LIGHTMAP. It would not crash and it would not look obviously broken -
// it would look like a panel whose bumps follow the baked lighting instead of
// the sawtooth. So: panel 6 must be indistinguishable from panel 0. If it bumps
// differently, the bundle selection is wrong.
textures/qltest/lightmapfirst
{
	qer_editorimage textures/qltest/tangent
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/tangent
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}
