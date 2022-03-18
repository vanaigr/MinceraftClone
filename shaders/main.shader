#version 430

//#extension GL_ARB_conservative_depth: warn
//#define GL_ARB_conservative_depth 1

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform uvec2 windowSize;

uniform vec3 rightDir, topDir;

in vec4 gl_FragCoord;

layout (depth_greater) out float gl_FragDepth;
layout(location = 0) out vec4 color;

uniform float time;

uniform sampler2D atlas;
uniform vec2 atlasTileCount;

uniform float mouseX;

uniform mat4 projection; //from local space to screen

uniform float near;
uniform float far;

layout(binding = 1) restrict readonly buffer ChunksIndices {
     uint data[][16*16*16/2]; //indeces are shorts 	//8192
};

flat in int startChunkIndex;

struct Block {
	uint id;
};

layout(binding = 2) restrict readonly buffer AtlasDescription {
    int positions[]; //16bit xSide, 16bit ySide; 16bit xTop, 16bit yTop; 16bit xBot, 16bit yBot 
};

struct Ray {
    vec3 orig;
    vec3 dir;
};

layout(binding = 5) restrict readonly buffer ChunksNeighbours {
    int neighbours[];
} ns;

//copied from Chunks.h
	ivec3 indexAsNeighbourDir(const int neighbourIndex) {
		return ivec3(
			(neighbourIndex / 1) % 3,
			(neighbourIndex / 3) % 3,
			(neighbourIndex / 9) % 3
		) - 1;
	}
	int dirAsNeighbourIndex(const ivec3 dir) {
		return dir.x+1 + (dir.y+1)*3 + (dir.z+1)*9;
	}
	int mirrorNeighbourDir(const int index) {
		return dirAsNeighbourIndex( -indexAsNeighbourDir(index) );
	}
	
	bool isNeighbourSelf(const int index) {
		return indexAsNeighbourDir(index) == 0;
	}
	
//void chunkNeighbours(const int chunkIndex, out int neighbours_out[27]) {
//	const int index = chunkIndex * 27;
//	for(int i = 0; i < 27; i ++) {
//		neighbours_out[index] = ns.neighbours[index + i];
//	}
//}

int chunkNeighbourIndex(const int chunkIndex, const ivec3 dir) {
	const int index = chunkIndex * 27;
	const int offset = dirAsNeighbourIndex(dir);
	return -(ns.neighbours[index + offset] + 1);
}


uniform ivec3 playerChunk;
uniform  vec3 playerInChunk;

//copied from Chunks.h
#define chunkDim 16
	
layout(binding = 3) restrict readonly buffer ChunksPoistions {
    int positions[];
} ps;

ivec3 chunkPosition(const int chunkIndex) {
	const uint index = chunkIndex * 3;
	return ivec3(
		ps.positions[index+0],
		ps.positions[index+1],
		ps.positions[index+2]
	);
}

vec3 relativeChunkPosition(const int chunkIndex) {
	return vec3( (chunkPosition(chunkIndex) - playerChunk) * chunkDim ) - playerInChunk;
}


vec3 sampleAtlas(const vec2 offset, const vec2 coord) {
    vec2 uv = vec2(
        coord.x + offset.x,
        coord.y + atlasTileCount.y - (offset.y + 1)
    ) / atlasTileCount;
    return texture2D(atlas, uv).rgb;
}

vec2 atlasAt(const uint id, const ivec3 side) {
	const int positions_[] = {
		0, 0, 0,
		0, 1, 2
	};
	const int offset = int(side.y == 1) + int(side.y == -1) * 2;
	const int index = (int(id) * 3 + offset);
	const int pos = positions[index];
	const int bit16 = 65535;
	return vec2( pos&bit16, (pos>>16)&bit16 );
}

ivec3 testBounds(const ivec3 i) {
	#define test(i) ( int(i >= 0) - int(i < chunkDim) )
		return ivec3(test(i.x), test(i.y), test(i.z));
	#undef ch16
}

bool checkBoundaries(const ivec3 i) {
	return all(equal( testBounds(i), ivec3(0) ));
}


uint blockAt(const int chunkIndex, const ivec3 i_v) {
	const int index = i_v.x + i_v.y * 16 + i_v.z * 16 * 16;
	const int packedIndex = index / 2;
	const int offset = (index % 2) * 16;
	const uint id = (data[chunkIndex][packedIndex] >> offset) & 65535;
	return id;
}

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

vec3 at(const Ray r, const float t) {
    return r.orig + r.dir * t;
}

void swap(inout vec3 v1, inout vec3 v2) {
    vec3 t = v1;
    v1 = v2;
    v2 = t;
}

void swap(inout float v1, inout float v2) {
    float t = v1;
    v1 = v2;
    v2 = t;
}

struct BlockIntersection {
	ivec3 index;
	ivec3 side;
	vec2 uv;
	float t;
	uint id;

};


struct Optional_BlockIntersection {
	bool is;
	BlockIntersection it;
};

Optional_BlockIntersection emptyOptional_BlockIntersection() {
	Optional_BlockIntersection a;
	return a;
}

Optional_BlockIntersection isInters(const Ray ray, const int chunkIndex) { 
	const vec3 dir = ray.dir;
	const ivec3 dir_ = ivec3(sign(dir));
	const ivec3 positive_ = max(+dir_, ivec3(0,0,0));
	const ivec3 negative_ = max(-dir_, ivec3(0,0,0));
	
	const vec3 stepLength = 1 / abs(dir);
	
	const  vec3 firstCellRow_f = vec3(positive_) * ceil(ray.orig) + vec3(negative_) * floor(ray.orig - 1);//floor(ray.orig + dir_);
	const ivec3 firstCellRow   = ivec3(firstCellRow_f); 
	const  vec3 firstCellDiff  = abs(ray.orig-firstCellRow_f - negative_); //distance to the frist border
	
	const vec3 border0Dist = -min(ray.orig, vec3(0,0,0)) * dir_; //steps to get to coord 0 if firstCell < 0
	const vec3 border16Dist = -max(ray.orig-16, vec3(0,0,0)) * dir_; //steps to get to coord 15 if firstCell > 15
	
	const vec3 maxBorderDiff = max(border0Dist, border16Dist);
	const ivec3 minSteps_vec = ivec3(floor(maxBorderDiff));
	const vec3 minLen_vec = maxBorderDiff*stepLength;
	
	const float minLen = max(max(minLen_vec.x, minLen_vec.y), minLen_vec.z);
	
	const bvec3 outside = lessThan(minLen_vec, vec3(minLen,minLen,minLen));
	
	ivec3 curSteps = 
		 ivec3(not(outside)) * ivec3(minSteps_vec) +
		 ivec3(   (outside)) * ivec3(max(ceil(minLen * abs(dir) - firstCellDiff),0)); //genIType mix(genIType x, genIType y, genBType a); - since version 4.50
	
	vec3 curLen = stepLength * firstCellDiff + stepLength * curSteps;
	
	const ivec3 farBoundaries = positive_ * 17 - 1;
	
	int curChunkIndex = chunkIndex;
	ivec3 relativeToChunk = ivec3(0);
	
	int i = 0;
	
	for(; i < 100; i++) {
		const float minCurLen = min(min(curLen.x, curLen.y), curLen.z);
		const bvec3 minAxis_b = equal(curLen, vec3(1,1,1) * minCurLen);
		const  vec3 minAxis_f = vec3(minAxis_b);
		const ivec3 minAxis_i = ivec3(minAxis_b);
		
		const  ivec3 otherAxis_i = ivec3(not(minAxis_b));
		const vec3 curCoordF_ = at(ray, minCurLen);
		const ivec3 curCoord_ = ivec3(floor(curCoordF_));
		
		const ivec3 cellAt_ =  
				+   minAxis_i * (firstCellRow + curSteps*dir_)
				+ otherAxis_i * curCoord_;
				
		//ivec3 cellAt = cellAt_ - relativeToChunk * chunkDim;
		vec3  curCoordF = curCoordF_ - relativeToChunk * chunkDim;
	    ivec3 curCoord = curCoord_ - relativeToChunk * chunkDim;
		
		const bvec3 inBounds = lessThanEqual((cellAt_ - relativeToChunk * chunkDim - farBoundaries) * dir_, ivec3(0,0,0));
		if( !all(inBounds) ) {
			const ivec3 neighbourDir = ivec3(!inBounds) * dir_;
			const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, neighbourDir);
			if(candChunkIndex < 0) return emptyOptional_BlockIntersection();
			
			curChunkIndex = candChunkIndex;
			relativeToChunk += neighbourDir;
			
			//cellAt = cellAt_ - relativeToChunk * chunkDim;
			curCoordF = curCoordF_ - relativeToChunk * chunkDim;
			curCoord =  curCoord_ - relativeToChunk * chunkDim;	
		}
		
		const ivec3 checks = ivec3( (equal(curCoordF, curCoord) || minAxis_b) );
		
		for(int x = checks.x; x >= 0; x --) {
			for(int y = checks.y; y >= 0; y --) {
				for(int z = checks.z; z >= 0; z --) {
					const ivec3 ca = cellAt_ - ivec3(x, y, z) * dir_;
					
					int curChunkIndex_ = curChunkIndex;
					ivec3 relativeToChunk_ = relativeToChunk;
					ivec3 ca_ = ca - relativeToChunk_ * chunkDim;
					
					const ivec3 outDir = testBounds(ca_);
					if( !all(equal(outDir, ivec3(0))) ) {
						const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, outDir);
						if(candChunkIndex < 0) continue;
						
						curChunkIndex_ = candChunkIndex;
						relativeToChunk_ += outDir;
						
						ca_ = ca - relativeToChunk_ * chunkDim;
					}
					
					const uint blockId = blockAt(curChunkIndex_, ca_);	
					if(blockId != 0) { 
						const vec3 blockCoord = curCoordF - curCoord;
						vec2 uv = vec2(
							dot(minAxis_f, blockCoord.zxx),
							dot(minAxis_f, blockCoord.yzy)
						);
						ivec3 side = -minAxis_i*dir_;
						
						if(blockId != 5 || blockId == 5 && int(dot(ivec2(uv * vec2(4, 8)), vec2(1))) % 2 == 0)
							return Optional_BlockIntersection(
								true,    
								BlockIntersection(
									ca_,
									side,
									uv,
									minCurLen,
									blockId
								)
							);
					}
				}	
			}
		}
		
		curSteps += minAxis_i;
		curLen += minAxis_f * stepLength;
	}

	return emptyOptional_BlockIntersection();
}

void main() {
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const vec3 relativeChunkPos = relativeChunkPosition(startChunkIndex);
	
	const Ray ray = Ray(-relativeChunkPos, rayDir);
	
	const Optional_BlockIntersection intersection = isInters(ray, startChunkIndex);
	float t = 1.0/0.0;
	vec4 col;
	if(intersection.is) {
		const BlockIntersection i = intersection.it;
		t = i.t;
		const vec2 uv = ((i.uv-0.5f) * 0.9999f)+0.5f;
		const uint blockId = i.id;
		
		const vec2 offset = atlasAt(blockId, i.side);
		col = vec4(sampleAtlas(offset, uv), 1 );

		const float shading = map(dot(normalize(vec3(1)), normalize(vec3(i.side))), -1, 1, 0.6, 0.9);
		
		col = vec4(col.xyz * shading, col.w);
	}
	else col = vec4(0,0,0,0);
	
	const float zWorld = dot(forwardDir, rayDir) * t;
	const vec4 proj = projection * vec4(0, 0, zWorld, 1);
	const float z = ( (1.0 / (proj.z) - 1.0 / (near)) / (1.0 / (far) - 1.0 / (near)) );

	if(length(gl_FragCoord.xy - windowSize / 2) < 3) {
		color = vec4(vec3(0.98), 1);
		gl_FragDepth = 0;
	}
	else if(zWorld <= far && intersection.is) {
		color = vec4(col.rgb, 1);
		gl_FragDepth = z;
	}
	else discard;
}		