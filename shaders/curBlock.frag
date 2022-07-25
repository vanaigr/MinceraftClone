#version 430
in vec4 gl_FragCoord;

uniform sampler2D font;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
};

//ERROR '=' : global const initializers must be constant ' const float'
/*const*/ float aspect = float(windowSize.y) / windowSize.x;
/*const*/ vec2 size = vec2(aspect, 1) * 0.06;
/*const*/ vec2 end_ = vec2(1 - 0.02 * aspect, 1-0.02);
/*const*/ vec2 start_ = vec2( end_ - size );
/*     */
/*const*/ vec2 startPos = floor(start_ * windowSize) / windowSize * 2 - 1;
/*const*/ vec2 endPos   = floor(end_   * windowSize) / windowSize * 2 - 1;

out vec4 color;

uniform uint block;
uniform float atlasTileSize;
uniform sampler2D atlas;

restrict readonly buffer AtlasDescription {
	int positions[]; //16bit xSide, 16bit ySide; 16bit xTop, 16bit yTop; 16bit xBot, 16bit yBot 
};

vec3 sampleAtlas(const vec2 offset, const vec2 coord) { //copied from main.frag
	const ivec2 size = textureSize(atlas, 0);
	const vec2 textureCoord = vec2(coord.x + offset.x, offset.y + 1-coord.y);
	const vec2 uv_ = vec2(textureCoord * atlasTileSize / vec2(size));
	const vec2 uv = vec2(uv_.x, 1 - uv_.y);
	return pow(texture(atlas, uv).rgb, vec3(2.2));
}

vec2 atlasAt(const uint id, const ivec3 side) { //copied from main.frag
	const int offset = int(side.y == 1) + int(side.y == -1) * 2;
	const int index = (int(id) * 4 + offset);
	const int pos = positions[index];
	const int bit16 = 65535;
	return vec2( pos&bit16, (pos>>16)&bit16 );
}

vec3 col(vec2 coord) {
	const vec2 pos = (coord / windowSize) * 2 - 1;
	const vec2 uv = (pos - startPos) / (endPos - startPos);
	
	const vec2 offset = atlasAt(block, ivec3(1,0,0));
	return sampleAtlas(offset, clamp(uv, 0.0001, 0.9999));
}

float rand(const vec2 co) {
	return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 sampleN(const vec2 coord, const uint n, const vec2 startRand) {
	const vec2 pixelCoord = floor(coord);
	const float fn = float(n);

	vec3 result = vec3(0);
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < n; j++) {
			const vec2 curCoord = pixelCoord + vec2(i / fn, j / fn);
			const vec2 offset = vec2(rand(startRand + curCoord.xy), rand(startRand + curCoord.xy + i+1)) / fn;
			const vec2 offsetedCoord = curCoord + offset;

			const vec3 sampl = col(offsetedCoord);
			result += sampl;
		}
	}

	return result / (fn * fn);
}

void main() {
	const vec3 col = sampleN(gl_FragCoord.xy, 6, vec2(block));
	color = vec4(col, 1);
}