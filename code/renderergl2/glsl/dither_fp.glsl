// Output dither.
//
// The frame is computed at much higher precision than it is displayed at, and
// the whole of that precision is thrown away in one step when it is written to
// the window. Rounding each pixel to the nearest representable value is what
// puts visible bands across skies, fog and coloured lighting: a smooth ramp
// crossing one quantisation boundary snaps for every pixel at once, so the
// boundary is a hard edge across the screen.
//
// Adding under half a step of noise before that rounding breaks the boundary
// up. Pixels either side land on different sides of it in a fine pattern the
// eye integrates back into the value that was actually there. It costs one
// texture read's worth of arithmetic and recovers most of a bit of precision.
//
// This is the same principle 3dfx's 22-bit mode used at 16-bit, applied at
// the only place it still pays on modern hardware - and the amplitude is
// derived from the framebuffer the driver actually gave us, not assumed, so
// a 10-bit output gets a quarter of the noise an 8-bit output does rather
// than four times what it needs.
//
// u_Color.r  one quantisation step of the real output framebuffer
// u_Color.g  temporal phase, advanced per frame (0 = static)
// u_Color.b  pattern: 1 ordered 8x8 Bayer, 2 interleaved gradient noise

uniform sampler2D u_DiffuseMap;
uniform vec4      u_Color;

varying vec2      var_Tex1;

// Ordered 8x8 Bayer. Deterministic and completely stable frame to frame, which
// is what you want if any shimmer at all is objectionable. Its weakness is that
// the pattern is regular enough to be visible as cross-hatching on very flat
// gradients.
float Bayer2x2(vec2 a)
{
	a = floor(a);
	return fract(a.x * 0.5 + a.y * a.y * 0.75);
}

float Bayer8x8(vec2 pos)
{
	// The recursive definition of an ordered dither matrix collapses into
	// this: each level is the level below at half the scale, weighted a
	// quarter, plus the 2x2 base. Result is in [0,1).
	float b4 = Bayer2x2(pos * 0.25) * 0.25 + Bayer2x2(pos);
	return Bayer2x2(pos * 0.5) * 0.25 + b4 * 0.25;
}

// Interleaved gradient noise. Much closer to blue noise than an ordered matrix
// - its error is concentrated in high spatial frequencies, which is exactly
// where the eye and any display filtering discard it - and it is three
// instructions with no lookup table.
float InterleavedGradientNoise(vec2 pos)
{
	return fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715))));
}

void main()
{
	vec4 color = texture2D(u_DiffuseMap, var_Tex1);

	float step = u_Color.r;

	if (step > 0.0)
	{
		vec2 pos = gl_FragCoord.xy;
		float noise;

		if (u_Color.b < 1.5)
		{
			noise = Bayer8x8(pos);
		}
		else
		{
			// Offsetting by the golden ratio each frame decorrelates
			// successive frames, so the pattern averages out over time
			// instead of standing still on the surface. At the frame
			// rates this runs at it reads as smooth rather than noisy,
			// and it is worth roughly another bit.
			noise = InterleavedGradientNoise(pos + u_Color.g);
		}

		// centre on zero: the point is to move values across the rounding
		// boundary in both directions, not to brighten the image
		color.rgb += (noise - 0.5) * step;
	}

	gl_FragColor = color;
}
