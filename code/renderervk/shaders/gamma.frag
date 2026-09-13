#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const float gamma = 1.0;
layout(constant_id = 1) const float obScale = 2.0;
layout(constant_id = 2) const float greyscale = 0.0;
//
layout(constant_id = 7) const int ditherMode = 0; // 0 - disabled, 1 - ordered
layout(constant_id = 8) const int depth_r = 255;
layout(constant_id = 9) const int depth_g = 255;
layout(constant_id = 10) const int depth_b = 255;
layout(constant_id = 11) const int toneMap = 0; // 0 - clip, 1 - roll off

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

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;

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
	if ( toneMap == 1 )
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

	if ( ditherMode == 1 ) {
		out_color.rgb = dither(out_color.rgb);
	}
}
