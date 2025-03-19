#version 420

in vec2 uv;
out vec4 color;

uniform float atlasTileSize;
uniform sampler2D atlas;

vec3 sampleAtlas(const vec2 offset, const vec2 coord) { //copied from main.frag
	const ivec2 size = textureSize(atlas, 0);
	const vec2 textureCoord = vec2(coord.x + offset.x, offset.y + 1-coord.y);
	const vec2 uv_ = vec2(textureCoord * atlasTileSize / vec2(size));
	const vec2 uv = vec2(uv_.x, 1 - uv_.y);
	return pow(texture(atlas, uv).rgb, vec3(2.2));
}

void main() {
	const vec3 value = sampleAtlas(vec2(31), uv);
	if(dot(value, vec3(1)) / 3 > 0.9) discard;
	color = vec4(value, 0.1);
}
