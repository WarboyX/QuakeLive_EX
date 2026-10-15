#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const float gamma = 1.0;
layout(constant_id = 1) const float obScale = 2.0;
layout(constant_id = 2) const float greyscale = 0.0;
//
layout(constant_id = 7) const int ditherMode = 0; // 0 - disabled, 1 - ordered, 2 - temporal (E198)

/* [QL] E198: the frame number, for r_dither 2. A push constant rather than a
   specialization constant because it changes every frame and a pipeline must
   not. Pushed before every draw with this shader (present and capture). */
layout(push_constant) uniform Push {
	uint frame;
} pc;
layout(constant_id = 8) const int depth_r = 255;
layout(constant_id = 9) const int depth_g = 255;
layout(constant_id = 10) const int depth_b = 255;
layout(constant_id = 11) const int toneMap = 0; // 0 - clip, 1 - roll off, 2 - filmic (E158)
layout(constant_id = 12) const int fxaaMode = 0;      // [QL] E157 r_fxaa: 0 off, 1 on
layout(constant_id = 13) const float sharpen = 0.0;   // [QL] E157 r_sharpen: 0 off .. 1 strongest

const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

const int bayerSize = 8;
const float bayerMatrix[bayerSize * bayerSize] = {
	0,  32, 8,  40, 2,  34, 10, 42,
	48, 16, 56, 24, 50, 18, 58, 26,
	12, 44, 4,  36, 14, 46, 6,  38,
	60, 28, 52, 20, 62, 30, 54, 22,
	3,  35, 11, 43, 1,  33, 9,  41,
	51, 19, 59, 27, 49, 17, 57, 25,
	15, 47, 7,  39, 13, 45, 5,  37,
	63, 31, 55, 23, 61, 29, 53, 21
};

float threshold() {
	ivec2 coordDenormalized = ivec2(gl_FragCoord.xy);
	ivec2 bayerCoord = coordDenormalized % bayerSize;
	float bayerSample = bayerMatrix[bayerCoord.x + bayerCoord.y * bayerSize];
	float threshold = (bayerSample + 0.5) / float(bayerSize * bayerSize);
	/* [QL] E198, r_dither 2. The ordered pattern is fixed to the screen, so on
	   a gradient that moves - or a camera that does - it reads as a fine grid
	   standing still while the picture slides under it. Stepping every
	   threshold by the golden ratio's fraction each frame walks each pixel
	   through the whole 0..1 range evenly over a few frames, so the pattern
	   averages away on a high refresh display and what is left is the gradient
	   the 8-bit output cannot otherwise hold. */
	if ( ditherMode == 2 ) {
		threshold = fract( threshold + float( pc.frame & 255u ) * 0.6180340 );
	}
	return threshold;
}

vec3 dither(vec3 color) {
	ivec3 depth = ivec3(depth_r, depth_g, depth_b);
	vec3 cDenormalized = color * depth;
	vec3 cLow = floor(cDenormalized);
	vec3 cFractional = cDenormalized - cLow;
	vec3 cDithered = cLow + step(threshold(), cFractional);
	return cDithered / depth;
}

/*
[QL] E157: FXAA, for when there is no multisampling.

Offsets are in the SOURCE image's texels, which is not the screen's when
r_renderScale renders smaller and this pass scales up - the edge is in the
source, so that is where it has to be found.

Skipped where local contrast is below the threshold, so flat areas and faint
detail come through untouched and only real edges pay for the extra taps.
Runs on the finished frame, so it also softens the HUD's text edges a little;
there is no pass between the 3D and the 2D to put it in.
*/
float luma(vec3 c) {
	return dot(c, vec3(0.299, 0.587, 0.114));
}

/*
The blend taps land between texels, and FXAA depends on them being filtered
there. This pass's sampler is nearest whenever the image is not being scaled
up (vk.blitFilter), which snapped each tap to one side of the edge and left
thin lines dotted instead of smoothed. Filtered by hand, so the result does
not depend on which sampler the scaling mode picked.
*/
vec3 bilinear(vec2 uv) {
	ivec2 size = textureSize(texture0, 0);
	vec2 p = uv * vec2(size) - 0.5;
	ivec2 i = ivec2(floor(p));
	vec2 f = p - floor(p);
	ivec2 hi = size - ivec2(1);
	vec3 a = texelFetch(texture0, clamp(i,               ivec2(0), hi), 0).rgb;
	vec3 b = texelFetch(texture0, clamp(i + ivec2(1, 0), ivec2(0), hi), 0).rgb;
	vec3 c = texelFetch(texture0, clamp(i + ivec2(0, 1), ivec2(0), hi), 0).rgb;
	vec3 d = texelFetch(texture0, clamp(i + ivec2(1, 1), ivec2(0), hi), 0).rgb;
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 fxaa(vec2 uv, vec2 px, vec3 rgbM) {
	/* FXAA 3.11's quality path: find the edge, walk along it to both ends,
	   and blend across it by where this pixel sits between them - plus a
	   sub-pixel term for detail thinner than a pixel. The compact variant
	   (direction from the four diagonals, blend along it) turned one-pixel
	   lines into dashes; this keeps them continuous. */
	const float steps[10] = float[10](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);

	float lM = luma(rgbM);
	float lN = luma(texture(texture0, uv + vec2( 0.0, -1.0) * px).rgb);
	float lS = luma(texture(texture0, uv + vec2( 0.0,  1.0) * px).rgb);
	float lW = luma(texture(texture0, uv + vec2(-1.0,  0.0) * px).rgb);
	float lE = luma(texture(texture0, uv + vec2( 1.0,  0.0) * px).rgb);
	float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
	float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
	float range = lMax - lMin;

	if (range < max(0.0312, lMax * 0.125)) {
		return rgbM;                // no edge worth the taps
	}

	float lNW = luma(texture(texture0, uv + vec2(-1.0, -1.0) * px).rgb);
	float lNE = luma(texture(texture0, uv + vec2( 1.0, -1.0) * px).rgb);
	float lSW = luma(texture(texture0, uv + vec2(-1.0,  1.0) * px).rgb);
	float lSE = luma(texture(texture0, uv + vec2( 1.0,  1.0) * px).rgb);

	/* sub-pixel aliasing: how far this pixel is from its neighbourhood */
	float lAvg = (2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0;
	float sub = clamp(abs(lAvg - lM) / range, 0.0, 1.0);
	sub = smoothstep(0.0, 1.0, sub);
	sub = sub * sub * 0.75;

	/* which way the edge runs */
	float edgeH = abs(-2.0 * lW + lNW + lSW) + 2.0 * abs(-2.0 * lM + lN + lS) + abs(-2.0 * lE + lNE + lSE);
	float edgeV = abs(-2.0 * lN + lNW + lNE) + 2.0 * abs(-2.0 * lM + lW + lE) + abs(-2.0 * lS + lSW + lSE);
	bool horz = edgeH >= edgeV;

	/* and which side of this pixel it is on */
	float l1 = horz ? lN : lW;
	float l2 = horz ? lS : lE;
	float g1 = abs(l1 - lM);
	float g2 = abs(l2 - lM);
	float stepLen = horz ? px.y : px.x;
	float lLocal;
	if (g1 >= g2) {
		stepLen = -stepLen;
		lLocal = 0.5 * (l1 + lM);
	} else {
		lLocal = 0.5 * (l2 + lM);
	}
	float gScaled = 0.25 * max(g1, g2);

	/* walk both ways along the edge, on the boundary between the two sides */
	vec2 cur = uv;
	if (horz) cur.y += stepLen * 0.5; else cur.x += stepLen * 0.5;
	vec2 off = horz ? vec2(px.x, 0.0) : vec2(0.0, px.y);
	vec2 uv1 = cur - off;
	vec2 uv2 = cur + off;
	float e1 = luma(bilinear(uv1)) - lLocal;
	float e2 = luma(bilinear(uv2)) - lLocal;
	bool done1 = abs(e1) >= gScaled;
	bool done2 = abs(e2) >= gScaled;

	for (int i = 0; i < 10 && !(done1 && done2); i++) {
		if (!done1) {
			uv1 -= off * steps[i];
			e1 = luma(bilinear(uv1)) - lLocal;
			done1 = abs(e1) >= gScaled;
		}
		if (!done2) {
			uv2 += off * steps[i];
			e2 = luma(bilinear(uv2)) - lLocal;
			done2 = abs(e2) >= gScaled;
		}
	}

	/* blend by how far this pixel is from the nearer end */
	float d1 = horz ? (uv.x - uv1.x) : (uv.y - uv1.y);
	float d2 = horz ? (uv2.x - uv.x) : (uv2.y - uv.y);
	bool dir1 = d1 < d2;
	float dist = min(d1, d2);
	float thickness = d1 + d2;
	float pixOff = -dist / thickness + 0.5;
	bool centreSmaller = lM < lLocal;
	bool correct = ((dir1 ? e1 : e2) < 0.0) != centreSmaller;
	float finalOff = max(correct ? pixOff : 0.0, sub);

	vec2 fuv = uv;
	if (horz) fuv.y += finalOff * stepLen; else fuv.x += finalOff * stepLen;
	return bilinear(fuv);
}

/*
[QL] E157: contrast-adaptive sharpening, after AMD's CAS.

A cross of four neighbours, weighted negatively by an amount that shrinks
where local contrast is already high - so soft areas sharpen and hard edges
do not ring. What it is for: r_renderScale's upscale and FXAA both soften,
and this puts detail back without the halos of a plain unsharp mask. Values
above full brightness (the floating-point target) get no sharpening, which is
where CAS's model does not apply.
*/
vec3 cas(vec2 uv, vec2 px, vec3 centre, vec3 raw) {
	vec3 a = texture(texture0, uv + vec2(-1.0, 0.0) * px).rgb;
	vec3 b = texture(texture0, uv + vec2( 1.0, 0.0) * px).rgb;
	vec3 c = texture(texture0, uv + vec2(0.0, -1.0) * px).rgb;
	vec3 d = texture(texture0, uv + vec2(0.0,  1.0) * px).rgb;
	vec3 mn = min(raw, min(min(a, b), min(c, d)));
	vec3 mx = max(raw, max(max(a, b), max(c, d)));
	vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1.0 / 1024.0)), 0.0, 1.0));
	vec3 w = amp * (-1.0 / mix(8.0, 5.0, clamp(sharpen, 0.0, 1.0)));
	return max((centre + w * (a + b + c + d)) / (1.0 + 4.0 * w), vec3(0.0));
}

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;

	if ( fxaaMode != 0 || sharpen > 0.0 ) {
		vec2 px = 1.0 / vec2(textureSize(texture0, 0));
		vec3 raw = base;
		if ( fxaaMode != 0 ) {
			base = fxaa(frag_tex_coord, px, raw);
		}
		if ( sharpen > 0.0 ) {
			base = cas(frag_tex_coord, px, base, raw);
		}
	}

	if ( greyscale == 1 )
	{
		base = vec3(dot(base, sRGB));
	}
	else if ( greyscale != 0 )
	{
		vec3 luma = vec3(dot(base, sRGB));
		base = mix(base, luma, greyscale);
	}

	if ( gamma != 1.0 )
	{
		base = pow(base, vec3(gamma));
	}

	/*
	[QL] The response curve, applied to whatever the scene target handed over.

	toneMap 0 is the original: multiply by obScale and let the hardware clamp.

	toneMap 1 is real time shading. It matters what is upstream: with r_rts the
	scene target is floating point, so values above full brightness arrived here
	intact instead of being discarded when they were written, and this is where
	they get brought down.

	The curve is identity below a knee and bends only above it. That flat
	section is the entire point, and the first attempt at this got it wrong - it
	used Reinhard, which has no flat section anywhere. Reinhard pulls midtones
	down everywhere: a value of 0.1 at obScale 4 comes back 0.293 where it
	should be 0.4. The menu went dark and the game went washed out, in exchange
	for headroom that nothing down there ever needed.

	Above the knee: knee + (1-knee)(1 - exp(-(c-knee)/(1-knee))). It meets the
	identity line at the knee with a matching slope, so there is no seam where
	it takes over, and it approaches 1.0 without arriving. Everything below the
	knee is bit for bit what it would have been with the curve off - which is
	the property that lets this be turned on without the picture changing except
	where it used to clip.
	*/
	/*
	[QL] E158: toneMap 2 is a filmic curve - Narkowicz's fit of the ACES
	reference transform. Unlike the knee it reshapes the whole range, not only
	the top: shadows lift a little, midtones gain contrast, and full white comes
	down to about 0.8 with the highlights above it rolled in rather than cut.
	That is the look, and it is why it is an option and not the default - it
	also applies to the HUD and menus, which are drawn into the same frame.
	*/
	if ( toneMap == 2 )
	{
		vec3 c = max(base * obScale, vec3(0.0));
		out_color = vec4(clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0), 1);
	}
	else if ( toneMap == 1 )
	{
		const float knee = 0.8;
		vec3 c = max(base * obScale, vec3(0.0));
		vec3 over = max(c - knee, vec3(0.0));
		out_color = vec4(min(c, vec3(knee)) +
			(1.0 - knee) * (1.0 - exp(-over / (1.0 - knee))), 1);
	}
	else
	{
		out_color = vec4(base * obScale, 1);
	}

	if ( ditherMode != 0 ) {
		out_color.rgb = dither(out_color.rgb);
	}
}
