// [QL] The console had no backdrop.
//
// Something in the search path defines `console` against gfx/misc/console01.tga
// and console02.tga - Quake 3's names. Quake Live's pak has neither, so both
// stages were dropped for a missing image, which left a shader with no stages,
// which RE_RegisterShader reports as 0. cl_main.c asks for it by name for
// cls.consoleShader, so the console drew its text straight over the game with
// nothing behind it.
//
// The same two images exist in this pak under different names -
// textures/sfx/console01.jpg and console02.jpg, both confirmed in
// docs/pak-manifest.txt - so this is the Quake 3 shader pointed at the files
// that are actually here. Two layers scrolling against each other at different
// rates, which is what gives it motion without animating anything.
//
// Darkened deliberately: this sits behind white text at whatever brightness
// r_rts and overbright leave it, and the stock pair is bright enough to read
// through.
console
{
	nopicmip
	{
		map textures/sfx/console01.jpg
		blendFunc GL_ONE GL_ZERO
		tcMod scroll 0.03 0.007
		rgbGen const ( 0.30 0.30 0.36 )
	}
	{
		map textures/sfx/console02.jpg
		blendFunc GL_ONE GL_ONE
		tcMod scroll -0.01 -0.003
		rgbGen const ( 0.16 0.16 0.20 )
	}
}
