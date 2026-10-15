#version 450

/*
[QL] The second half of the water reflection: put it on the scene.

Two passes and not one, and the reason is a rule rather than a preference: the
trace samples the scene colour and this writes to it, and a pass cannot read the
attachment it is writing. So the trace resolves the reflection into an offscreen
image and this blends that image in.

Which also means the mask does not have to be recomputed. The trace wrote alpha
0 everywhere the pixel was not water or the ray found nothing, so an ordinary
source-alpha blend does the whole job - no plane test here, no second copy of
the water definition to keep in step with the first.

No tone mapping, no scaling: this runs before the present pass, in whatever
space the scene target is in, and the colour came out of that same target.
*/

layout(set = 0, binding = 0) uniform sampler2D reflectionMap;

/* [QL] E151: 1.0, or 0.5 for the half-resolution trace. Id 20 because these
   pipelines are also handed the gamma shader's block, which maps ids 0-11 - an
   id nothing else supplies stays at this default. */
layout(constant_id = 20) const float uvScale = 1.0;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

void main() {
	/*
	[QL] E151, r_ssrResolution. At half resolution the trace fills only the
	top-left quarter of reflectionMap, and uvScale is 0.5 - its own pipeline,
	so the setting can change live. Bilinear filtering is the upsample; the
	clamp keeps it off the texels just past the quarter, which still hold
	whatever an earlier full-resolution frame left there.
	*/
	vec2 full = vec2(textureSize(reflectionMap, 0));
	vec2 uv = min(frag_tex_coord * uvScale, (full * uvScale - 0.5) / full);
	out_color = texture(reflectionMap, uv);

	/*
	[QL] Belt and braces, and it earns its place.

	The blend is source-alpha, so alpha 0 is already a no-op arithmetically -
	but only if the alpha that arrives is the alpha the trace wrote. If the
	offscreen target ever ends up in a format without an alpha channel, reads of
	it return 1.0, and every pixel on screen would be replaced by a mostly-black
	reflection image instead of being left alone. The target takes
	vk.color_format, and B10G11R11 - which r_rts 2 selects - has no alpha.

	Discarding on zero makes "no reflection here" mean no write at all, rather
	than a write that happens to multiply out.
	*/
	if ( out_color.a <= 0.0 ) {
		discard;
	}
}
