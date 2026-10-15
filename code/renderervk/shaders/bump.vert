#version 450

// [QL] R25. The static bump pass - see bump.frag for what it is and why it is a
// second pass rather than part of the world shader.

layout(push_constant) uniform Transform {
	mat4 mvp;
};

layout(location = 0) in vec4 in_position;
layout(location = 2) in vec2 in_tex_coord0;	// the normal map's uv (diffuse bundle)
layout(location = 3) in vec2 in_tex_coord1;	// the lightmap's uv, which the deluxemap shares
layout(location = 5) in vec4 in_normal;

layout(location = 0) out vec2 tc0;
layout(location = 1) out vec2 tc1;
layout(location = 2) out vec3 N;
layout(location = 3) out vec3 P;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = mvp * vec4(in_position.xyz, 1.0);
	tc0 = in_tex_coord0;
	tc1 = in_tex_coord1;
	N = in_normal.xyz;
	P = in_position.xyz;
}
