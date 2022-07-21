#version 460

layout(pixel_center_integer) in vec4 gl_FragCoord;
out vec4 outColor;

uniform sampler2D sampler;
uniform bool h;

//https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
const float weight[] = { 0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162 };

void main() {
	ivec2 coord = ivec2(floor(gl_FragCoord.xy));
	
	vec4 color = texelFetch(sampler, coord, 0) * weight[0];
	
	for (int i = 1; i < 5; i++) {
		if(h) {
			color += texelFetch(sampler, coord + ivec2(0, i), 0) * weight[i];
			color += texelFetch(sampler, coord - ivec2(0, i), 0) * weight[i];   
		}			
		else {
			color += texelFetch(sampler, coord + ivec2(i, 0), 0) * weight[i];
			color += texelFetch(sampler, coord - ivec2(i, 0), 0) * weight[i];
		}
	}
	
	outColor = color;
}

/*layout(pixel_center_integer) in vec4 gl_FragCoord;
out vec4 outColor;

uniform sampler2D sampler;
uniform bool h;

//https://github.com/lisyarus/compute/blob/master/blur/source/compute_separable.cpp

const int M = 16;
const int N = 2 * M + 1;
// sigma = 10
const float coeffs[N] = float[N](
	0.012318109844189502,
	0.014381474814203989,
	0.016623532195728208,
	0.019024086115486723,
	0.02155484948872149,
	0.02417948052890078,
	0.02685404941667096,
	0.0295279624870386,
	0.03214534135442581,
	0.03464682117793548,
	0.0369716985390341,
	0.039060328279673276,
	0.040856643282313365,
	0.04231065439216247,
	0.043380781642569775,
	0.044035873841196206,
	0.04425662519949865,
	0.044035873841196206,
	0.043380781642569775,
	0.04231065439216247,
	0.040856643282313365,
	0.039060328279673276,
	0.0369716985390341,
	0.03464682117793548,
	0.03214534135442581,
	0.0295279624870386,
	0.02685404941667096,
	0.02417948052890078,
	0.02155484948872149,
	0.019024086115486723,
	0.016623532195728208,
	0.014381474814203989,
	0.012318109844189502
);
void main() {
	ivec2 pixel_coord = ivec2(floor(gl_FragCoord.xy));
	
	vec4 sum = vec4(0.0);
	for (int i = 0; i < N; ++i) {
		const ivec2 pc = pixel_coord + (h ? ivec2(1, 0) : ivec2(0, 1)) * (i - M);
		sum += coeffs[i] * texelFetch(sampler, pc, 0);
	}
	outColor = sum;
}*/