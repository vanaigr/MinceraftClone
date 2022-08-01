#version 420
in vec4 gl_FragCoord;

in vec2 startPos;
in vec2 endPos;

in vec2 startUV;
in vec2 endUV;

in vec4 color;

out vec4 outColor;

uniform sampler2D font;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
};

float col(vec2 coord) {
	const vec2 pos = (coord / windowSize) * 2 - 1;
	const vec2 uv = startUV + (pos - startPos) / (endPos - startPos) * (endUV - startUV);
	
	return texture(font, clamp(uv, startUV, endUV)).b;
}

void main() {
	const float c = col(gl_FragCoord.xy);
	const float bias = 0.48;
	const float steepness = 8;
	const float a = clamp((c - bias) * steepness + bias, 0, 1);
	outColor = vec4(color.rgb, color.a * a);
}