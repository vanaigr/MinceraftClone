#version 430


void main() {
	const vec2 verts[] = {
		vec2(-1),
		vec2(1, -1),
		vec2(-1, 1),
		vec2(1)
	};
	
	gl_Position = vec4( verts[gl_VertexID], 0.0, 1 );
}