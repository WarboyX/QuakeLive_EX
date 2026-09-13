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
	[QL] Overbright, with or without somewhere for the top of the range to go.

	toneMap 0 is the original: multiply and let the hardware clamp. Every value
	above 1/obScale lands on exactly 1.0, so at obScale 4 the top three quarters
	of the range become one colour and any surface already near white has
	nothing added to it - which is what makes an additive effect drawn over a
	lit paper screen invisible.

	toneMap 1 is Reinhard with a white point, and the white point is obScale
	itself: 0 maps to 0, obScale maps to 1, and the curve is near enough linear
	at the bottom that shadows and midtones keep the brightening they were asked
	for. What changes is the top - it compresses towards white instead of
	arriving there and stopping, so bright surfaces stay distinguishable from
	each other and still have room above them.

	Per channel rather than on luminance. Luminance-based tone mapping keeps
	saturation better, but it also shifts hue on anything that clips in one
	channel only, and Quake's palette does that constantly - a saturated red
	lamp, a green rail trail. Per channel is the more predictable of the two
	here.
	*/
	if ( toneMap == 1 )
	{
		vec3 c = base * obScale;
		float w = max(obScale, 1.0);
		out_color = vec4((c * (1.0 + c / (w * w))) / (1.0 + c), 1);
	}
	else
	{
		out_color = vec4(base * obScale, 1);
	}

	if ( ditherMode == 1 ) {
		out_color.rgb = dither(out_color.rgb);
	}
}
