/*
[QL] R27. Stochastic hex-tiling: the fix for "the same texture over and over".

WHERE THIS COMES FROM

The request was for Minecraft's "natural textures", which in OptiFine is a
per-block random rotation and flip: a wall of stone picks one of 4, 2 or 8
symmetries of its texture from the block's coordinates, so the grid of identical
faces stops reading as a grid. That mechanism does not transfer to this engine
and it is worth saying why rather than half-porting it. Minecraft's world is
blocks, so "the texture of this block" is a thing you can rotate independently
of its neighbours; the seam it creates falls on a block boundary you were
already going to see. A Quake Live wall is ONE polygon with continuous UVs, and
there is no boundary to hide a rotation on - rotate part of it and you get a
visible cut, which is the problem, not the fix.

The continuous-surface form of the same idea is stochastic tiling, from Heitz
and Neyret's "High-Performance By-Example Noise using a Histogram-Preserving
Blending Operator" (HPG 2018), in the real-time form Mikkelsen published as
"Practical Real-Time Hex-Tiling" (JCGT 11(3), 2022). It does what OptiFine does
- randomise the texture per cell - but blends three overlapping cells so there
is never an edge between them:

  - lay a triangle lattice over UV space; every point falls in a triangle whose
    three corners are the centres of the three nearest hexagons
  - give each hexagon a random UV offset and rotation from its integer index
  - sample the texture three times, once per hexagon, and blend by barycentrics

Because the offsets are random and the blend is smooth, the output has the
input's look and none of its period. Mikkelsen's contribution over the original
is dropping histogram-preservation - which needs a precomputed lookup texture
per source image, and we have neither the pipeline nor the licence to bake one
for Quake Live's art - and replacing it with a contrast ramp on the blend
weights. That is what makes this usable here at all: it samples the ORIGINAL
texture, so it works on any image the engine already loaded.

WHAT IT COSTS AND WHERE IT LIES

Three samples instead of one, plus the lattice arithmetic. That is why it is
opt-in and not the default.

And it is wrong for most textures, which is the part worth understanding before
turning it on everywhere. Blending three randomly-offset copies of an image
assumes the image is stochastic - moss, granite, sand, rubble, dirt. Give it a
texture with STRUCTURE and it destroys the structure: brick courses stop lining
up, a floor plate's rivets smear, a sign becomes three overlapping signs. This
is exactly why OptiFine ships natural.properties as a per-texture allow-list
rather than a global switch, and it is why r_qlNaturalTextures 1 waits to be
asked by a shader. Mode 2 exists to see the effect on a whole map at once, not
to play with.

The other known limitation, and it is Mikkelsen's own: a normal map whose
average normal does not point near +Z will show its tilt once three rotated
copies are blended, because the rotation no longer cancels. Ours are generated
from height fields by central differences, so their average is +Z by
construction. An authored map from elsewhere may not be.
*/

#ifndef QL_HEXTILE_GLSL
#define QL_HEXTILE_GLSL

/*
The lattice. st is scaled by 2*sqrt(3) and skewed onto a simplex grid, which is
the same trick simplex noise uses: in skewed space the triangle a point belongs
to is found by flooring, and which of the two triangles in the cell is settled
by whether the barycentric third coordinate went negative.

w1/w2/w3 come out as the barycentrics and vertex1..3 as the integer hexagon
indices, which are what everything else here is keyed on - so the same point in
UV space always draws the same three hexagons no matter where the camera is.
*/
void QL_TriangleGrid( out float w1, out float w2, out float w3,
                      out ivec2 vertex1, out ivec2 vertex2, out ivec2 vertex3,
                      vec2 st )
{
	st *= 3.4641016151; // 2*sqrt(3)

	// column-major in GLSL: this is the matrix [[1,-0.57735027],[0,1.15470054]]
	// applied as gridToSkewedGrid * st
	const mat2 gridToSkewedGrid = mat2( 1.0, 0.0, -0.57735027, 1.15470054 );
	vec2 skewedCoord = gridToSkewedGrid * st;

	ivec2 baseId = ivec2( floor( skewedCoord ) );
	vec3 temp = vec3( fract( skewedCoord ), 0.0 );
	temp.z = 1.0 - temp.x - temp.y;

	float s = step( 0.0, -temp.z );
	float s2 = 2.0 * s - 1.0;

	w1 = -temp.z * s2;
	w2 = s - temp.y * s2;
	w3 = s - temp.x * s2;

	vertex1 = baseId + ivec2( s, s );
	vertex2 = baseId + ivec2( s, 1.0 - s );
	vertex3 = baseId + ivec2( 1.0 - s, s );
}

// Per-hexagon UV offset. Any hash with no visible structure will do; this is
// the one from the reference, and its structure (or lack of it) is the only
// thing standing between this and a visible repeat of its own.
vec2 QL_HexHash( vec2 p )
{
	vec2 r = vec2( dot( p, vec2( 127.1, 311.7 ) ),
	               dot( p, vec2( 269.5, 183.3 ) ) );
	return fract( sin( r ) * 43758.5453 );
}

// Centre of a hexagon in unskewed UV space, so the rotation below turns about
// the cell rather than about the origin.
vec2 QL_HexCenST( ivec2 vertex )
{
	const mat2 invSkewMat = mat2( 1.0, 0.0, 0.5, 0.86602540 ); // 1/1.15470054
	return ( invSkewMat * vec2( vertex ) ) * 0.28867513;       // / (2*sqrt(3))
}

/*
Per-hexagon rotation. The angle is an arbitrary function of the integer index
folded into +/-pi and then scaled, so rotStrength 0 is "offsets only" and 1 is
"any angle". Offsets alone already break a repeat; rotation is what stops the
three copies sharing a direction, which matters most on anything with a grain.
*/
mat2 QL_HexRot( ivec2 idx, float rotStrength )
{
	float angle = abs( float( idx.x * idx.y ) ) + abs( float( idx.x + idx.y ) ) + 3.14159265;

	angle = mod( angle, 6.28318531 );
	if ( angle < 0.0 ) angle += 6.28318531;
	if ( angle > 3.14159265 ) angle -= 6.28318531;

	angle *= rotStrength;

	float cs = cos( angle ), si = sin( angle );
	return mat2( cs, si, -si, cs ); // column-major: [[cs,-si],[si,cs]]
}

/*
The contrast ramp that stands in for histogram preservation.

Blending three samples of anything averages away its variance, and an averaged
texture reads as flat and slightly grey. Heitz and Neyret solved that exactly,
with a per-texture precomputed histogram transform. This pushes the blend
WEIGHTS towards 0 and 1 instead, so most of the surface is mostly one sample.
r > 0.5 sharpens, r < 0.5 softens, r == 0.5 is the plain blend, and Mikkelsen
recommends 0.7-0.8.

Measured, on the cobble sheet at the stone corridor's scale, it does much less
than that story implies: plain tiling is 26.2 luminance stddev, this is 29.7 at
r=0.5 and 30.7 at 0.75. Three percent between the two ends, and both ABOVE
plain rather than below. The reason is the pow(w,7) below - by the time Gain3
sees the weights the barycentric falloff has already made them nearly one-hot,
so there is little averaging left to undo. Kept at the reference's value
because it is free and marginally better, not because it is doing the work the
paragraph above credits it with.
*/
vec3 QL_HexGain3( vec3 x, float r )
{
	float k = log( 1.0 - r ) / log( 0.5 );

	vec3 s = 2.0 * step( vec3( 0.5 ), x );
	vec3 m = 2.0 * ( 1.0 - s );

	vec3 res = 0.5 * s + 0.25 * m * pow( max( vec3( 0.0 ), s + x * m ), vec3( k ) );

	return res / ( res.x + res.y + res.z );
}

/*
Colour. Three textureGrad fetches - GRAD and not an ordinary sample, because
each copy is rotated and the hardware's own derivatives of st would be the
derivatives of the UNROTATED coordinate. Get that wrong and mip selection is
wrong per cell, which shows up as three differently-blurred copies of the
texture fighting each other, worst at exactly the grazing angles this engine
spends its time at.

Luminance weighting (the Dw term) biases the blend towards the brighter of the
three, which is what keeps a dark sample from washing out a bright one; the
pow(w, 7) makes the barycentric falloff sharp enough that the transition region
is small.
*/
vec4 QL_HexSample( sampler2D tex, vec2 st, float rotStrength, float contrast )
{
	vec2 dSTdx = dFdx( st ), dSTdy = dFdy( st );

	float w1, w2, w3;
	ivec2 vertex1, vertex2, vertex3;
	QL_TriangleGrid( w1, w2, w3, vertex1, vertex2, vertex3, st );

	mat2 rot1 = QL_HexRot( vertex1, rotStrength );
	mat2 rot2 = QL_HexRot( vertex2, rotStrength );
	mat2 rot3 = QL_HexRot( vertex3, rotStrength );

	vec2 cen1 = QL_HexCenST( vertex1 );
	vec2 cen2 = QL_HexCenST( vertex2 );
	vec2 cen3 = QL_HexCenST( vertex3 );

	// v * M in GLSL is the row-vector product, which is what the reference's
	// mul(v, M) means - NOT M * v. Getting this backwards rotates every cell
	// the wrong way, which looks plausible and is wrong.
	vec2 st1 = ( st - cen1 ) * rot1 + cen1 + QL_HexHash( vec2( vertex1 ) );
	vec2 st2 = ( st - cen2 ) * rot2 + cen2 + QL_HexHash( vec2( vertex2 ) );
	vec2 st3 = ( st - cen3 ) * rot3 + cen3 + QL_HexHash( vec2( vertex3 ) );

	vec4 c1 = textureGrad( tex, st1, dSTdx * rot1, dSTdy * rot1 );
	vec4 c2 = textureGrad( tex, st2, dSTdx * rot2, dSTdy * rot2 );
	vec4 c3 = textureGrad( tex, st3, dSTdx * rot3, dSTdy * rot3 );

	const vec3 Lw = vec3( 0.299, 0.587, 0.114 );
	vec3 Dw = vec3( dot( c1.xyz, Lw ), dot( c2.xyz, Lw ), dot( c3.xyz, Lw ) );

	Dw = mix( vec3( 1.0 ), Dw, 0.6 );
	vec3 W = Dw * pow( vec3( w1, w2, w3 ), vec3( 7.0 ) );
	W /= ( W.x + W.y + W.z );
	if ( contrast != 0.5 ) W = QL_HexGain3( W, contrast );

	return W.x * c1 + W.y * c2 + W.z * c3;
}

/*
Normal map, and this one cannot be done as a colour blend.

Averaging three unit normals gives a shorter vector pointing somewhere between
them, and renormalising it does not recover the slope - blend a left-facing and
a right-facing normal and you get +Z, a flat surface, when what the surface
actually has is two slopes meeting. So the three are converted to DERIVATIVES
(dh/du, dh/dv) first, which do average correctly because height is additive,
and the result is turned back into a normal at the end. That is Mikkelsen's
bumphex2derivNMap.

The weighting term differs from the colour case for the same reason: brightness
means nothing here, so the bias is towards the sample with the STEEPEST slope -
sqrt(D/(1+D)) is the sine of the angle off +Z - which keeps a flat patch from
erasing a detailed one.

Alpha is carried through as a plain weighted blend. In this tree that is the
HEIGHT the parallax march steps through, and height IS additive, so a weighted
average of three heights is the right answer without any of the above.
*/
/*
The single strongest hexagon at this point, as a transform.

For anything that has to MARCH the texture rather than sample it once - the
parallax loop below is thirty-two fetches - three blended copies is not an
option at any sensible cost. The march runs in one cell instead: the one with
the largest barycentric weight, which is the cell the blend is mostly showing
anyway. Inside a cell that is exactly right; near a boundary it is the
neighbour's height field rather than a mix of two, and since the shading there
is a mix of two, the depth and the shading disagree slightly over a band a few
pixels wide. That is a real approximation and it is worth naming, but it is
invisible next to the alternative, which is three times the march.

The caller transforms its uv into this cell's space, marches, and transforms the
RESULT back - rot is a rotation, so its inverse is its transpose, which is what
the row-vector product gives for free.
*/
void QL_HexDominant( vec2 st, float rotStrength, out mat2 rot, out vec2 cen, out vec2 off )
{
	float w1, w2, w3;
	ivec2 vertex1, vertex2, vertex3;
	QL_TriangleGrid( w1, w2, w3, vertex1, vertex2, vertex3, st );

	ivec2 best = vertex1;
	float bw = w1;
	if ( w2 > bw ) { bw = w2; best = vertex2; }
	if ( w3 > bw ) { bw = w3; best = vertex3; }

	rot = QL_HexRot( best, rotStrength );
	cen = QL_HexCenST( best );
	off = QL_HexHash( vec2( best ) );
}

/*
Tangent-space normal -> slope (dh/du, dh/dv). Dividing by z is the whole of it;
the rest is guarding the divide. abs() because a normal map texel that decodes
with a negative z is malformed but should still produce a finite slope in the
right direction rather than a sign flip, and the 1/128 floor bounds the result
to +/-128 so one bad texel cannot poison a three-way blend with an inf.

QL_DerivToNormal is its exact inverse, so a hex sample of a flat normal map
comes back flat, which is the first thing worth checking if this ever looks
wrong.
*/
vec2 QL_NormalToDeriv( vec3 vM )
{
	vec3 vMa = abs( vM );
	float z_ma = max( vMa.z, ( 1.0 / 128.0 ) * max( vMa.x, vMa.y ) );
	return -vM.xy / z_ma;
}

vec3 QL_DerivToNormal( vec2 deriv )
{
	return normalize( vec3( -deriv, 1.0 ) );
}

vec4 QL_HexSampleNormal( sampler2D nmap, vec2 st, float rotStrength, float contrast )
{
	vec2 dSTdx = dFdx( st ), dSTdy = dFdy( st );

	float w1, w2, w3;
	ivec2 vertex1, vertex2, vertex3;
	QL_TriangleGrid( w1, w2, w3, vertex1, vertex2, vertex3, st );

	mat2 rot1 = QL_HexRot( vertex1, rotStrength );
	mat2 rot2 = QL_HexRot( vertex2, rotStrength );
	mat2 rot3 = QL_HexRot( vertex3, rotStrength );

	vec2 cen1 = QL_HexCenST( vertex1 );
	vec2 cen2 = QL_HexCenST( vertex2 );
	vec2 cen3 = QL_HexCenST( vertex3 );

	vec2 st1 = ( st - cen1 ) * rot1 + cen1 + QL_HexHash( vec2( vertex1 ) );
	vec2 st2 = ( st - cen2 ) * rot2 + cen2 + QL_HexHash( vec2( vertex2 ) );
	vec2 st3 = ( st - cen3 ) * rot3 + cen3 + QL_HexHash( vec2( vertex3 ) );

	vec4 n1 = textureGrad( nmap, st1, dSTdx * rot1, dSTdy * rot1 );
	vec4 n2 = textureGrad( nmap, st2, dSTdx * rot2, dSTdy * rot2 );
	vec4 n3 = textureGrad( nmap, st3, dSTdx * rot3, dSTdy * rot3 );

	vec2 d1 = QL_NormalToDeriv( 2.0 * n1.xyz - 1.0 );
	vec2 d2 = QL_NormalToDeriv( 2.0 * n2.xyz - 1.0 );
	vec2 d3 = QL_NormalToDeriv( 2.0 * n3.xyz - 1.0 );

	// each derivative was measured in its own rotated frame; bring them back
	d1 = rot1 * d1;
	d2 = rot2 * d2;
	d3 = rot3 * d3;

	vec3 D = vec3( dot( d1, d1 ), dot( d2, d2 ), dot( d3, d3 ) );
	vec3 Dw = sqrt( D / ( 1.0 + D ) );

	Dw = mix( vec3( 1.0 ), Dw, 0.6 );
	vec3 W = Dw * pow( vec3( w1, w2, w3 ), vec3( 7.0 ) );
	W /= ( W.x + W.y + W.z );
	if ( contrast != 0.5 ) W = QL_HexGain3( W, contrast );

	vec2 deriv = W.x * d1 + W.y * d2 + W.z * d3;
	float height = W.x * n1.w + W.y * n2.w + W.z * n3.w;

	return vec4( QL_DerivToNormal( deriv ) * 0.5 + 0.5, height );
}

#endif // QL_HEXTILE_GLSL
