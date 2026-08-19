// [QL] Freeze Tag ice shell.
//
// The frozen overlay used to be sprites/frozen painted flat over the player
// model, which washed the whole character out to a pale blue silhouette.
//
// This is the same trick the quad shell uses: the model is drawn a second time
// with a customShader, and the shader is an additive environment map, so it
// reads as a coating catching the light rather than a colour wash. Compare
// powerups/quad, which is one additive tcGen environment stage.
//
// The image is Quake Live's own ice environment map, referenced by name. Only
// the name lives here - no asset content is redistributed, same rule as
// docs/pak-manifest.txt.
//
// White rather than blue: rgbGen identity keeps the map's own values, and the
// second stage adds a slow-moving specular sheen so a statue is readable at a
// glance without hiding the player's team colours underneath.

powerups/freezeshell
{
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen identity
		tcGen environment
	}
	{
		map textures/effects/icemap.jpg
		blendfunc GL_ONE GL_ONE
		rgbGen wave sin 0.25 0.15 0 0.4
		tcGen environment
		tcMod rotate 12
	}
}
