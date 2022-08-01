#version 460
in vec2 uv;

out vec4 color;

void main() {
	const vec2 pos = uv*2-1;
	const float bias = 0.85 * 0.85;
	color = vec4(1, 1, 1, clamp(1 - float(dot(pos, pos) - bias) * 3 + bias, 0, 1));
}