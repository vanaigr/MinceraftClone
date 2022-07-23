#version 460

layout(pixel_center_integer) in vec4 gl_FragCoord;
out vec4 outColor;

uniform sampler2D sampler;
uniform sampler2D blurSampler;
uniform float exposure;

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


void main() {	
	const vec3 color = texelFetch(sampler    , ivec2(floor(gl_FragCoord.xy)), 0).rgb;
	const vec3 bloom = texelFetch(blurSampler, ivec2(floor(gl_FragCoord.xy)), 0).rgb;
	
	vec3 result = rgb2hsv(1.0 - exp(-(color + bloom) * exposure));
	result = hsv2rgb(vec3(
		result.x,
		result.y / pow(result.z*0.01+1, 5),
		result.z
	));
	result = pow(result, vec3(1.0 / 2.2));
	
	outColor = vec4(result, 1.0);
}