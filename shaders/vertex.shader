#version 420
uniform mat4 projection;
uniform mat4 toLocal;
uniform bool isInChunk;

uniform float near;
uniform float far;

in layout(location = 0) vec3 relativeChunkPos_;
in layout(location = 1) uint positions_;
in layout(location = 2) uint chunkIndex_;

out vec3 relativeChunkPos;
flat out uint chunkIndex;

//copied from Chunks.h
#define chunkDim 16u

vec3 indexBlock(const uint index) { //copied from Chunks.h
	return vec3( index % chunkDim, (index / chunkDim) % chunkDim, (index / chunkDim / chunkDim) );
}
vec3 start(const uint data) { return indexBlock(data&65535u); } //copied from Chunks.h
vec3 end(const uint data) { return indexBlock((data>>16)&65535u); } //copied from Chunks.h
vec3 onePastEnd(const uint data) { return end(data) + 1; } //copied from Chunks.h
		
void main() {
	relativeChunkPos = relativeChunkPos_;
	chunkIndex = chunkIndex_;
	
	if(!isInChunk) {
		const vec3 startPos = start(positions_);
		const vec3 endPos = onePastEnd(positions_);
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
		const vec3 position = mix(startPos, endPos, xyz);
		
		const mat4 translation = {
			vec4(1,0,0,0),
			vec4(0,1,0,0),
			vec4(0,0,1,0),
			vec4(relativeChunkPos_, 1)
		};			
		
		const mat4 model_matrix = toLocal * translation;
		
		gl_Position = projection * (model_matrix * vec4(position, 1.0));
	}
	else {
		const vec2 verts[] = {
			vec2(-1),
			vec2(1, -1),
			vec2(-1, 1),
			vec2(1)
		};
		
		gl_Position = vec4( verts[gl_VertexID], 0.0, 1 );
	}
}