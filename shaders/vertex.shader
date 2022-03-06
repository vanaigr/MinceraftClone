#version 420
uniform mat4 projection;
uniform mat4 model_matrix;
uniform bool isInChunk;


uniform float near;
uniform float far;

void main() {
	if(!isInChunk) {
		int tri = gl_VertexID / 3;
		int idx = gl_VertexID % 3;
		int face = tri / 2;
		int top = tri % 2;
	
		int dir = face % 3;
		int pos = face / 3;
	
		int nz = dir >> 1;
		int ny = dir & 1;
		int nx = 1 ^ (ny | nz);
	
		vec3 d = vec3(nx, ny, nz);
		float flip = 1 - 2 * pos;
	
		vec3 n = flip * d;
		vec3 u = -d.yzx;
		vec3 v = flip * d.zxy;
	
		float mirror = -1 + 2 * top;
		vec3 xyz = n + mirror*(1-2*(idx&1))*u + mirror*(1-2*(idx>>1))*v;
		xyz = (xyz + 1) / 2;
		gl_Position = projection * (model_matrix * vec4(xyz, 1.0));
	}
	else {
		const vec2 verts[] = {
			vec2(-1),
			vec2(1, -1),
			vec2(-1, 1),
			
			vec2(-1, 1),
			vec2(1, -1),
			vec2(1)
		};
		
		gl_Position = vec4( verts[gl_VertexID], 0.0, 1 );
	}
}