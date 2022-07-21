#version 460

layout(pixel_center_integer) in vec4 gl_FragCoord;
out vec4 color;

uniform sampler2D sampler;
uniform sampler2D blurSampler;


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

float map(const float value, const float min1, const float max1, const float min2, const float max2) {
    return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

int mapInteger(int value) {//based on https://stackoverflow.com/a/24771093/18704284
    value *= 1664525;
    value += 101390223;
    value ^= value >> 11;
    value ^= value << 16;
    value ^= value >> 23;
    value *= 110351245;
    value += 12345;

    return value;
}

ivec3 mix3i(const ivec3 a, const ivec3 b, const ivec3 f) {
	return (1-f) * a + b * f;
}

int dot3i(const ivec3 a, const ivec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

bvec3 and3b/*seems that glsl has no && for bvec_*/(const bvec3 a, const bvec3 b) { return bvec3(a.x && b.x, a.y && b.y, a.z && b.z); }


vec3 colorMapping(vec3 col) {
	const float x = 10;
	const vec3 c = rgb2hsv(col);
	return hsv2rgb(vec3(
		c.x,
		(c.y+0.07) / pow(log(c.z + x) + 0.3, 1.0 / 15),
		pow(log(c.z + 1.0) / log(c.z + x), 1.55) * 1.6
	));
}

void main() {
	const vec3 c1 = texelFetch(sampler    , ivec2(floor(gl_FragCoord.xy)), 0).rgb;
	const vec3 c2 = texelFetch(blurSampler, ivec2(floor(gl_FragCoord.xy)), 0).rgb;
	
	const vec3 col = c1 + c2*0.2;
	
	color = vec4(colorMapping(col.rgb), 1.0);
	
	//if(length(gl_FragCoord.xy - windowSize / 2) < 3) color = vec4(vec3(0.98), 1);
	//else 
	//color = c2;
}