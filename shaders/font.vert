#version 420

layout(location = 0) in vec2 pos_s;
layout(location = 1) in vec2 pos_e;
layout(location = 2) in vec2 uv_s;
layout(location = 3) in vec2 uv_e;

out vec2 startPos;
out vec2 endPos;

out vec2 startUV;
out vec2 endUV;

void main() {
	const vec2 startPosition = vec2(pos_s.x*2 - 1, 1 - pos_s.y*2);
	const vec2 endPosition   = vec2(pos_e.x*2 - 1, 1 - pos_e.y*2);
	const vec2 interp = vec2(gl_VertexID % 2, gl_VertexID / 2);
	gl_Position = vec4(mix(startPosition, endPosition, interp), 0, 1);
	
	startPos = startPosition;
	endPos   = endPosition;
	startUV  = vec2(uv_s.x, 1 - uv_s.y);
	endUV    = vec2(uv_e.x, 1 - uv_e.y);
}