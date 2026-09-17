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

// =============================================================================
// R25 - qltest_bump.map. Authored normal maps under static light.
//
// All three carry a FLAT GREY diffuse on purpose. If the diffuse had any detail
// you could never tell shading from paint, and that ambiguity is exactly what
// made R20 Stage A's results hard to read on real art. Here anything you see is
// the normal map.
//
// normalMap is a stage keyword and belongs on the stage the light pass binds -
// the diffuse, which with lightmap-first ordering is stage 1. An explicit map
// always beats the derived one: FinishShader only derives when normalMap is
// still NULL.
// =============================================================================

// The diagnostic. A hemisphere fails a bad basis in two independent ways at
// once, and they have different fixes:
//   craters instead of domes      -> green channel convention (OpenGL vs DirectX)
//   lit on the side from the light -> mirrored tangent
textures/qltest/domes
{
	qer_editorimage textures/qltest/flatgrey
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/flatgrey
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/domes_n
	}
}

// The realistic case - value noise differentiated into normals. This is what
// the rock in the middle of the map wears, and what R25 is actually for.
textures/qltest/rocknoise
{
	qer_editorimage textures/qltest/flatgrey
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/flatgrey
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/rocknoise_n
	}
}

// The control. Same diffuse, no normal map, and qlNoPerturb so R20 cannot
// derive one either - otherwise this panel would quietly grow bumps from the
// flat grey and stop being a control.
textures/qltest/plaingrey
{
	qer_editorimage textures/qltest/flatgrey
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/flatgrey
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		qlNoPerturb
	}
}

// Room walls and ceiling. Plain, unlit-looking, deliberately uninteresting so
// the panels are what the eye goes to.
textures/qltest/flatgrey
{
	qer_editorimage textures/qltest/flatgrey
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/flatgrey
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		qlNoPerturb
	}
}

// -----------------------------------------------------------------------------
// R25 materials. Diffuse and normal map from one height field - see
// tools/gen-testmap-textures.py. These answer "does it look better"; the flat
// grey panels above answer "is it correct".
// -----------------------------------------------------------------------------

textures/qltest/mat_brick
{
	qer_editorimage textures/qltest/brick_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/brick_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/brick_n
	}
}

textures/qltest/mat_rock
{
	qer_editorimage textures/qltest/rock_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/rock_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/rock_n
	}
}

textures/qltest/mat_plate
{
	qer_editorimage textures/qltest/plate_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/plate_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/plate_n
	}
}

// =============================================================================
// R25 parallax showcase materials - qltest_stone.map.
//
// Diffuse and normal map from one height field (tools/gen-testmap-textures.py),
// with the HEIGHT in the normal map's alpha - that is what the parallax march
// steps through. Without it these are ordinary normal maps and the surface
// cannot self-occlude.
//
// normalMap sits on the diffuse stage, which with lightmap-first ordering is
// stage 1 - and is the stage CollapseMultitexture used to throw away.
// =============================================================================

// Deep square wells, vertical sides. The strongest parallax case there is - nothing self-occludes like a hole.
textures/qltest/mat_waffle
{
	qer_editorimage textures/qltest/waffle_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/waffle_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/waffle_n
	}
}

// Deep parallel channels. Directional: along them the offset barely moves, across them it swims.
textures/qltest/mat_grooves
{
	qer_editorimage textures/qltest/grooves_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/grooves_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/grooves_n
	}
}

// Eight equal terraces. The only sheet here readable as a number - at the right depth the risers meet the treads.
textures/qltest/mat_steps
{
	qer_editorimage textures/qltest/steps_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/steps_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/steps_n
	}
}

// Hemispheres proud of the surface. domes_n's realistic cousin: it has a diffuse, so it answers 'does this read as relief'.
textures/qltest/mat_studs
{
	qer_editorimage textures/qltest/studs_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/studs_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/studs_n
	}
}

// Irregular masonry, deeply recessed joints. Irregular on purpose - a regular grid reads as brick and the joints stop looking like depth.
textures/qltest/mat_stoneblock
{
	qer_editorimage textures/qltest/stoneblock_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/stoneblock_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/stoneblock_n
	}
}

// Rounded stones with deep gaps. The floor material: every gap is a hole the view ray climbs out of.
textures/qltest/mat_cobble
{
	qer_editorimage textures/qltest/cobble_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/cobble_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/cobble_n
	}
}

// Ridged noise, so creases are creases. What a seven-polygon block has to wear to read as a boulder.
textures/qltest/mat_boulder
{
	qer_editorimage textures/qltest/boulder_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/boulder_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/boulder_n
	}
}

// Fine and shallow, as the control. Parallax should be nearly invisible here; if it pops like the cobble, the depth is running off something other than the height field.
textures/qltest/mat_gravel
{
	qer_editorimage textures/qltest/gravel_d
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/qltest/gravel_d
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		normalMap textures/qltest/gravel_n
	}
}
