#version 430
uniform mat4 projection;
uniform mat4 toLocal;
uniform bool isInChunk;
uniform ivec3 playerChunk;
uniform  vec3 playerInChunk;

uniform float near;
uniform float far;

in layout(location = 0) int chunkIndex_;

out vec3 relativeChunkPos;
flat out int chunkIndex;

layout(binding = 3) restrict readonly buffer ChunksPoistions {
    int positions[];
} ps;

layout(binding = 4) restrict readonly buffer ChunksBounds {
    uint bounds[];
} bs;

ivec3 chunkPosition(const int chunkIndex) {
	const uint index = chunkIndex * 3;
	return ivec3(
		ps.positions[index+0],
		ps.positions[index+1],
		ps.positions[index+2]
	);
}

uint chunkBounds(const int chunkIndex) {
	return bs.bounds[chunkIndex];
}

//copied from Chunks.h
#define chunkDim 16

vec3 indexBlock(const uint data) { //copied from Chunks.h
	return vec3( data % chunkDim, (data / chunkDim) % chunkDim, (data / chunkDim / chunkDim) );
}
vec3 start(const uint data) { return indexBlock(data&65535u); } //copied from Chunks.h
vec3 end(const uint data) { return indexBlock((data>>16)&65535u); } //copied from Chunks.h
vec3 onePastEnd(const uint data) { return end(data) + 1; } //copied from Chunks.h

vec3 relativeChunkPosition(const int chunkIndex) {
	return vec3( (chunkPosition(chunkIndex) - playerChunk) * chunkDim ) - playerInChunk;
}
		
void main() {
	const vec3 relativePos = relativeChunkPosition(chunkIndex_);
	relativeChunkPos = relativePos;
	chunkIndex = chunkIndex_;
	
	if(!isInChunk) {
		const uint bounds = chunkBounds(chunkIndex_);
		const vec3 startPos = start(bounds);
		const vec3 endPos = onePastEnd(bounds);
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
			vec4(relativePos, 1)
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