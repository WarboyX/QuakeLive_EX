#version 450

/*
[QL] R25. Normal mapping on surfaces nothing is shooting at.

A lightmap stores how much light reached a texel and not which way it came
from, so there is nothing for a normal to be dotted against and no amount of
normal map changes a statically lit surface. q3map2 -deluxe writes a second
lightmap beside every lightmap holding the dominant light direction per texel,
and that is the missing operand. This pass is what reads it.

MODELSPACE. q3map2 writes tangentspace deluxemaps too (-deluxemode 1) against a
basis it builds from worldUp x normal, which is not the UV-derived basis below.
The two disagreeing would light every surface wrongly with nothing on screen to
say why, so the build script takes the default and this shader assumes it: the
direction decodes straight to world space, which is also where nN ends up, and
no frame has to agree with any other.

A SECOND PASS, not part of the world shader. The world draws through the
generic multitexture path, which already spends two of the three texture slots
this backend has on lightmap and diffuse. Deluxe and normal would need a fourth.
Multiplying what is already in the framebuffer needs only these two, costs one
more draw on surfaces that opt in, and cannot regress a surface that does not.

Note this is NOT the modulation that R20 ruled out. That was about removing
FACETING - a discontinuity baked across a face boundary, which cannot be divided
out by any smooth term. This adds detail within a face, which multiplication
does perfectly well.
*/

layout(set = 1, binding = 0) uniform sampler2D normalmap;
layout(set = 2, binding = 0) uniform sampler2D deluxemap;

layout(location = 0) in vec2 tc0;
layout(location = 1) in vec2 tc1;
layout(location = 2) in vec3 N;
layout(location = 3) in vec3 P;

layout(location = 0) out vec4 out_color;

// r_qlBumpScale, and the debug view. Specialization constants rather than a
// uniform because this pass has no uniform block of its own and both change
// rarely enough that rebuilding the pipeline is the cheaper price.
layout (constant_id = 0) const float bump_scale = 1.0;
layout (constant_id = 1) const int debug_mode = 0;

/*
Which texcoord set is which. A collapsed world shader puts the lightmap in
bundle 0 and the diffuse in bundle 1 when the lightmap stage came first, and the
other way round when it did not - both orderings are ordinary, and the vertex
attributes arrive in bundle order. Rather than guess, the C side says, and a
specialization constant costs nothing because the branch is compiled out.
*/
layout (constant_id = 2) const int tc_swap = 0;

void main() {
	vec2 uvN = (tc_swap != 0) ? tc1 : tc0;	// the normal map's, in the diffuse's uv
	vec2 uvD = (tc_swap != 0) ? tc0 : tc1;	// the deluxemap's, in the lightmap's uv

	vec3 geomN = normalize(N);

	// Modelspace direction, and it is not necessarily unit after filtering.
	vec4 dtex = texture(deluxemap, uvD);
	vec3 dir = dtex.xyz * 2.0 - 1.0;
	float dlen = length(dir);

	/*
	A deluxemap texel can be genuinely empty - a surface no light reached at
	all, which q3map2 leaves at zero rather than at some arbitrary direction.
	Normalising that gives a NaN and a black or white speckle. Anything this
	short carries no direction, so the pass has nothing to say about the texel
	and leaves it exactly as it found it.
	*/
	if (dlen < 0.1) {
		/*
		Black in the direction view, so "no data here" and "data here" are
		different colours. The first version returned white for both, which is
		also what an untouched surface looks like - so the one view that exists
		to answer "did the deluxemap arrive" could not answer it.
		*/
		if (debug_mode == 1) {
			out_color = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		out_color = vec4(1.0);
		return;
	}
	dir /= dlen;

	if (debug_mode == 1) {
		// the direction field itself, as colour
		out_color = vec4(dir * 0.5 + 0.5, 1.0);
		return;
	}

	/*
	Tangent basis from screen-space derivatives. Identical to light_frag.tmpl on
	purpose - Q3 BSP carries no tangents, and if the two passes built different
	bases the same normal map would light one way under a rocket and another way
	standing still. Cramer's rule on the 2x2 UV Jacobian; duv1.y, not duv2.x.
	*/
	vec3 dPdx = dFdx(P);
	vec3 dPdy = dFdy(P);
	vec2 duv1 = dFdx(uvN);
	vec2 duv2 = dFdy(uvN);

	float a01 = duv1.x * duv2.y;
	float b01 = duv2.x * duv1.y;
	float det = ((a01 >= b01) ? 1.0 : -1.0) * max(abs(a01 - b01), 1e-6);

	vec3 Traw = (duv2.y * dPdx - duv1.y * dPdy) / det;
	if (dot(Traw, Traw) < 1e-8) {
		vec3 up = (abs(geomN.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		Traw = cross(up, geomN);
	}

	vec3 Tn = normalize(Traw - geomN * dot(geomN, Traw));
	vec3 Bn = cross(geomN, Tn);

	vec3 tsn = normalize(texture(normalmap, uvN).xyz * 2.0 - 1.0);
	vec3 nN = normalize(Tn * tsn.x + Bn * tsn.y + geomN * tsn.z);

	if (debug_mode == 2) {
		out_color = vec4(nN * 0.5 + 0.5, 1.0);
		return;
	}

	/*
	A DIFFERENCE, not a ratio, and the first version of this got it wrong.

	The tempting form is bumped/flat: the lightmap has dot(geomN, dir) baked
	into it, so dividing that back out and multiplying the bumped term in looks
	like the exact answer. It is not, for two reasons. The lightmap is not
	intensity times one N.L - it is every light, plus shadowing, plus ambient,
	so there is no single term in there to divide out. And the quotient runs
	away exactly where the light is shallowest: floored at 0.35 it still pinned
	at the 2.0 clamp over most of a room, which is what "everything is white"
	was.

	The difference cannot run away. It is zero where the normal map is flat, so
	an unperturbed surface is untouched to the bit; it is symmetric about 1, so
	a bump brightens by as much as its far side darkens; and both terms are
	bounded, so the result is bounded before any clamp. The clamp below is a
	guard, not the mechanism - if it is doing work, the scale is too high.
	*/
	float flatTerm = dot(geomN, dir);
	float bumpTerm = dot(nN, dir);

	float ratio = 1.0 + (bumpTerm - flatTerm) * bump_scale;
	ratio = clamp(ratio, 0.25, 1.75);

	if (debug_mode == 3) {
		// the modulation alone, mid-grey being "unchanged"
		out_color = vec4(vec3(ratio * 0.5), 1.0);
		return;
	}

	out_color = vec4(vec3(ratio), 1.0);
}
