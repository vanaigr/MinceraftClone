#version 430

uniform vec2 startPos;
uniform vec2 endPos;

void main() {
	const vec2 verts[] = {
		vec2(0),
		vec2(1, 0),
		vec2(0, 1),
		vec2(1)
	};
	vec2 interp = verts[gl_VertexID];
	gl_Position = vec4(mix(startPos, endPos, interp), 0, 1);
}