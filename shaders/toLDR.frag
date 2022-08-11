#version 460

layout(pixel_center_integer) in vec4 gl_FragCoord;
out vec4 outColor;

uniform sampler2D sampler;
uniform sampler2D blurSampler;
uniform float exposure;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
	uint flags;
};

#define getFlag(INDEX) (bool((flags >> (INDEX)) & 1))

/*const*/ ivec2 fragCoord = ivec2(gl_FragCoord.xy);

vec3 rgb2hsv(const vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(const vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

const vec3 rgbToLum = vec3(0.2126, 0.7152, 0.0722);

//https://catlikecoding.com/unity/tutorials/advanced-rendering/fxaa/
float sampleLiminance(const int xO, const int yO) {
	return dot(texelFetch(sampler, fragCoord + ivec2(xO, yO), 0).rgb, rgbToLum);
}
vec3 calcColor() {
	if(getFlag(6)) return texelFetch(sampler, fragCoord, 0).rgb;
	
	const ivec2 imageSize = textureSize(sampler, 0);
	const vec2 texelSize = 1.0 / imageSize;
	if(any(lessThanEqual(abs(fragCoord - imageSize), ivec2(1)))) return texelFetch(sampler, fragCoord, 0).rgb;
	
	vec3 color;
	
	struct LuminanceData {
		float m, n, e, s, w;
		float ne, nw, se, sw;
	};
	
	LuminanceData l;
	l.sw = sampleLiminance(-1, -1);
	l.s  = sampleLiminance( 0, -1);
	l.se = sampleLiminance( 1, -1);
	l.w  = sampleLiminance(-1,  0);
	l.m = dot(color = texelFetch(sampler, fragCoord, 0).rgb, rgbToLum);
	l.e  = sampleLiminance( 1,  0);
	l.nw = sampleLiminance(-1,  1);
	l.n  = sampleLiminance( 0,  1);
	l.ne = sampleLiminance( 1,  1);
	
	const float highest = max(max(max(max(max(max(max(max(l.m, l.n), l.e), l.s), l.w), l.ne), l.nw), l.se), l.sw);
	const float lowest  = min(min(min(min(min(min(min(min(l.m, l.n), l.e), l.s), l.w), l.ne), l.nw), l.se), l.sw);
	const float contrast = (highest - lowest);
	
	if(contrast < 0.8 * lowest) {
		return color;
	}
	else {
		const float weightedLum = abs(
			(1.0 / 12) * (
			  2 * (l.n + l.e + l.s + l.w)
			  + l.ne + l.nw + l.se + l.sw
			)
			- l.m
		);
		const float factor = smoothstep(0, 1, clamp(weightedLum / contrast, 0, 1));
		const float blendFactor = factor * factor;
		
		const float horizontalWeightedDiff =
			abs(l.n + l.s - 2 * l.m) * 2 +
			abs(l.ne + l.se - 2 * l.e) +
			abs(l.nw + l.sw - 2 * l.w);
		const float verticalWeightedDiff =
			abs(l.e + l.w - 2 * l.m) * 2 +
			abs(l.ne + l.nw - 2 * l.n) +
			abs(l.se + l.sw - 2 * l.s);
		const bool blendVertical = verticalWeightedDiff < horizontalWeightedDiff;
		
		const float positiveLum = blendVertical ? l.n : l.e;
		const float negativeLum = blendVertical ? l.s : l.w;
		const float pGradient = abs(positiveLum - l.m);
		const float nGradient = abs(negativeLum - l.m);
		
		const vec2 offset = (pGradient > nGradient ? 1 : -1) * blendFactor * (blendVertical ? vec2(0, 1) : vec2(1, 0));

		return texture(sampler, (gl_FragCoord.xy + 0.5 + offset) * texelSize).rgb;
	}
}

void main() {	
	const vec3 color = calcColor();
	const vec3 bloom = texelFetch(blurSampler, fragCoord, 0).rgb;
	
	vec3 result = rgb2hsv(1.0 - exp(-(color + bloom) * exposure));
	result = hsv2rgb(vec3(
		result.x,
		result.y / pow(result.z*0.01+1, 5),
		result.z
	));
	result = pow(result, vec3(1.0 / 2.2));
	
	outColor = vec4(result, 1.0);
}