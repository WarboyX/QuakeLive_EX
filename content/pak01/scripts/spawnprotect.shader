// [QL] Spawn protection overlay.
//
// Applied as refEntity_t.customShader, which replaces the model's own shader
// for that draw - so the player renders as a lit, translucent silhouette of
// themselves rather than a translucent copy of their skin. That is what
// Counter-Strike's casual spawn protection looks like, and it is what the Q3
// shader system can actually express: customShader is a whole-model
// replacement, so keeping the skin would need per-model translucent variants
// or renderer support for forcing alpha onto an opaque shader, which this
// renderer does not have.
//
// alphaGen entity, so cgame sets the strength through refEntity_t.shaderRGBA[3]
// and the value lives in one place rather than being baked in here.
models/players/spawnprotect
{
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen lightingDiffuse
		alphaGen entity
	}
}
