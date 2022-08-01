#version 460

uniform float radius;
out vec2 uv;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
};

void main() {
	const float aspect = float(windowSize.y) / windowSize.x;
	const vec2 uv_ = vec2(gl_VertexID / 2, gl_VertexID % 2);
	uv = uv_;
	gl_Position = vec4((uv_-0.5) * vec2(aspect, 1) * radius, 0.0, 1.0);
}