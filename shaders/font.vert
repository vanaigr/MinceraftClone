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
	vec2 interp = vec2(gl_VertexID % 2, gl_VertexID / 2);
	gl_Position = vec4(mix(pos_s, pos_e, interp), 0, 1);
	
	startPos = pos_s;
	endPos   = pos_e;
	startUV  = uv_s ;
	endUV    = uv_e ;
}