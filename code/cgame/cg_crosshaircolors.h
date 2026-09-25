#ifndef CG_CROSSHAIRCOLORS_H
#define CG_CROSSHAIRCOLORS_H
/*
[QL] E131. cg_crosshairColor's 26 colours.

Quake Live's menu sets the cvar with a colour slider (ITEM_TYPE_SLIDER_COLOR,
cvarInt 1..26) drawn over menu/art/fx_base, a 256x16 gradient: red, orange,
yellow, green, cyan, blue, violet, magenta, pink, then a grey tail. Value v
puts the thumb at (v-1)/25 of the bar's width, so these were sampled from the
bar at exactly those points (tools/crosshair-colors.py; only the numbers are
here, never the image, which is in pak00).

Shared by cgame, which draws the crosshair in it, and ui, whose preview
beside the style row shows the same colour.

The bar is drawn shaded - bright in the middle, dim at both ends - so each
sample is scaled up to full brightness: the bar shows the hue, the crosshair
gets it at full strength, and cg_crosshairBrightness is what dims it. The grey
tail has no hue to scale, so 25, the default, is white (the crosshair Quake
Live draws out of the box) and 26, where the bar fades out, is black. Those two
are read off the bar's shape, not measured.
*/
#define CROSSHAIR_COLORS 26
#define CROSSHAIR_DEFAULT_COLOR 25
static const vec3_t cg_crosshairColors[CROSSHAIR_COLORS] = {
	{ 1.00f, 0.04f, 0.00f },	// 1
	{ 1.00f, 0.13f, 0.00f },	// 2
	{ 1.00f, 0.30f, 0.00f },	// 3
	{ 1.00f, 0.49f, 0.00f },	// 4
	{ 1.00f, 0.89f, 0.00f },	// 5
	{ 0.85f, 1.00f, 0.00f },	// 6
	{ 0.43f, 1.00f, 0.00f },	// 7
	{ 0.27f, 1.00f, 0.00f },	// 8
	{ 0.07f, 1.00f, 0.03f },	// 9
	{ 0.00f, 1.00f, 0.23f },	// 10
	{ 0.00f, 1.00f, 0.36f },	// 11
	{ 0.00f, 1.00f, 0.59f },	// 12
	{ 0.00f, 0.98f, 1.00f },	// 13
	{ 0.00f, 0.54f, 1.00f },	// 14
	{ 0.00f, 0.35f, 1.00f },	// 15
	{ 0.00f, 0.22f, 1.00f },	// 16
	{ 0.07f, 0.03f, 1.00f },	// 17
	{ 0.28f, 0.00f, 1.00f },	// 18
	{ 0.42f, 0.00f, 1.00f },	// 19
	{ 0.77f, 0.00f, 1.00f },	// 20
	{ 1.00f, 0.00f, 0.73f },	// 21
	{ 1.00f, 0.00f, 0.45f },	// 22
	{ 1.00f, 0.02f, 0.32f },	// 23
	{ 1.00f, 0.62f, 0.71f },	// 24
	{ 1.00f, 1.00f, 1.00f },	// 25
	{ 0.00f, 0.00f, 0.00f },	// 26
};

#endif
