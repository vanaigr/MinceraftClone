#version 420
in vec4 gl_FragCoord;

in vec2 startPos;
in vec2 endPos;

in vec2 startUV;
in vec2 endUV;


out vec4 color;

uniform sampler2D font;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
};

float col(vec2 coord) {
	const vec2 pos = (coord / windowSize) * 2 - 1;
	const vec2 uv = startUV + (pos - startPos) / (endPos - startPos) * (endUV - startUV);
	
	return texture2D(font, clamp(uv, startUV, endUV)).r;
}

float rand(const vec2 co) {
	return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float sampleN(const vec2 coord, const uint n, const vec2 startRand) {
	const vec2 pixelCoord = floor(coord);
	const float fn = float(n);

	float result = 0;
	for (uint i = 0; i < n; i++) {
		for (uint j = 0; j < n; j++) {
			const vec2 curCoord = pixelCoord + vec2(i / fn, j / fn);
			const vec2 offset = vec2(rand(startRand + curCoord.xy), rand(startRand + curCoord.xy + i+1)) / fn;
			const vec2 offsetedCoord = curCoord + offset;

			const float sampl = col(offsetedCoord);
			result += sampl;
		}
	}

	return result / (fn * fn);
}

void main() {
	const float col = sampleN(gl_FragCoord.xy, 4, startUV);
	color = vec4(vec3(0), 1-col);
}