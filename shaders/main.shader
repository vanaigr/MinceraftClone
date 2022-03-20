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

uniform int startChunkIndex;

uniform sampler2D worldColor;
uniform sampler2D worldDepth;

//copied from Chunks.h
#define chunkDim 16

layout(binding = 1) restrict readonly buffer ChunksIndices {
     uint data[][16*16*16/2]; //indeces are shorts 	//8192
};



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


layout(binding = 4) restrict readonly buffer ChunksBounds {
    uint bounds[];
} bs;

uint chunkBounds(const int chunkIndex) {
	return bs.bounds[chunkIndex];
}
ivec3 indexBlock(const uint data) { //copied from Chunks.h
	return ivec3( data % chunkDim, (data / chunkDim) % chunkDim, (data / chunkDim / chunkDim) );
}
ivec3 start(const uint data) { return indexBlock(data&65535u); } //copied from Chunks.h
ivec3 end(const uint data) { return indexBlock((data>>16)&65535u); } //copied from Chunks.h
ivec3 onePastEnd(const uint data) { return end(data) + 1; } //copied from Chunks.h
bool emptyBounds(const ivec3 start, const ivec3 onePastEnd) {
	return any(lessThanEqual(onePastEnd, start));
}


#define neighboursCount 6
layout(binding = 5) restrict readonly buffer ChunksNeighbours {
    int neighbours[];
} ns;

//copied from Chunks.h
	ivec3 indexAsNeighbourDir(const int neighbourIndex) {
		const ivec3 dirs[] = { ivec3(-1,0,0),ivec3(1,0,0),ivec3(0,-1,0),ivec3(0,1,0),ivec3(0,0,-1),ivec3(0,0,1) };
		return dirs[neighbourIndex];
	}
	int dirAsNeighbourIndex(const ivec3 dir) {
		return (dir.x+1)/2 + (dir.y+1)/2+abs(dir.y*2) + (dir.z+1)/2+abs(dir.z*4) ;
	}
	int mirrorNeighbourDir(const int index) {
		return dirAsNeighbourIndex( -indexAsNeighbourDir(index) );
	}
	
	bool isNeighbourSelf(const int index) {
		return indexAsNeighbourDir(index) == 0;
	}

int chunkDirectNeighbourIndex(const int chunkIndex, const ivec3 dir) {
	const int index = chunkIndex * neighboursCount;
	const int offset = dirAsNeighbourIndex(dir);
	return -(ns.neighbours[index + offset] + 1);
}

int chunkNeighbourIndex(const int chunkIndex, const ivec3 dir) {
	int outChunkIndex = chunkIndex;
	if(dir.x != 0) outChunkIndex = chunkDirectNeighbourIndex(outChunkIndex, ivec3(dir.x,0,0));
	if(dir.y != 0) outChunkIndex = chunkDirectNeighbourIndex(outChunkIndex, ivec3(0,dir.y,0));
	if(dir.z != 0) outChunkIndex = chunkDirectNeighbourIndex(outChunkIndex, ivec3(0,0,dir.z));
	return outChunkIndex;
}


uniform ivec3 playerChunk;
uniform  vec3 playerInChunk;

	
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
	//ivec3 index;
	ivec3 side;
	//bool backside;
	vec3 glassDir;
	vec3 color;
	vec3 at;
	//vec2 uv;
	float t;
	int chunkIndex;
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

vec3 refract(const vec3 incoming, const vec3 normal, const float ior)  { 
    float cosi = clamp(-1, 1, dot(incoming, normal));
    float etai = 1, etat = ior; 
    vec3 n = normal; 
    if (cosi < 0) { cosi = -cosi; } else { float tmp = etai; etai = etat; etat = tmp; n= -normal; } 
    const float eta = etai / etat; 
    const float k = 1 - eta * eta * (1 - cosi * cosi); 
    return k < 0 ? vec3(0) : eta * incoming + (eta * cosi - sqrt(k)) * n; 
} 

Optional_BlockIntersection isInters(const Ray ray, const int chunkIndex, const float maxDist) { 	
	const vec3 dir = ray.dir;
	const ivec3 dir_ = ivec3(sign(dir));
	const ivec3 positive_ = max(+dir_, ivec3(0,0,0));
	const ivec3 negative_ = max(-dir_, ivec3(0,0,0));
	
	const vec3 stepLength = 1 / abs(dir);
	
	const  vec3 firstCellRow_f = vec3(positive_) * (floor(ray.orig)+1) + vec3(negative_) * (ceil(ray.orig)-1);
	const ivec3 firstCellRow   = ivec3(firstCellRow_f); 
	const  vec3 firstCellDiff  = abs(ray.orig-firstCellRow_f); //distance to the frist block side
	
	
	ivec3 curSteps = ivec3(0);
	vec3 curLen = vec3(0);
	int curChunkIndex = chunkIndex;
	ivec3 relativeToChunk = ivec3(0);
	ivec3 farBoundaries;
	
	float allSkipMinLen = 1.0/0.0;
	bool allSkip = false;
	
	float minLength = 0;
	
	for(int t = 0; t < 100; t ++) {
		const uint bounds = chunkBounds(curChunkIndex);
		ivec3 startPos = start(bounds);
		ivec3 endPos = onePastEnd(bounds);
		const bool empty = emptyBounds(startPos, endPos);
		
		const vec3 relativeOrig = ray.orig - relativeToChunk * chunkDim;

		if(!empty) {
			farBoundaries = positive_ * (endPos - startPos+1) + startPos-1;
			
			const vec3 border0Dist  = -min(relativeOrig - startPos, vec3(0,0,0)) * dir_;
			const vec3 border16Dist = -max(relativeOrig - endPos, vec3(0,0,0)) * dir_;
			
			const vec3 maxBorderDiff = max(border0Dist, border16Dist);
			const ivec3 minSteps_vec = ivec3(floor(maxBorderDiff));
			const vec3 minLen_vec = maxBorderDiff*stepLength;
			
			const float minLen = max(max(minLen_vec.x, minLen_vec.y), minLen_vec.z); //minimal length for all rays to get inside of chunk bounds
			const bvec3 outside = lessThan(minLen_vec, vec3(minLen));
				
			curSteps = 
				ivec3(not(outside)) * ivec3(minSteps_vec) +
				ivec3(   (outside)) * ivec3(max(ceil(minLen * abs(dir) - firstCellDiff),0));
				
			curLen = stepLength * curSteps + stepLength * firstCellDiff;
			
			const vec3 testAt =  
				+ vec3(not(outside)) * (firstCellRow - negative_ + curSteps*dir_)
				+ vec3(   (outside)) * at(ray, minLen);
				
			const bvec3 inBounds = lessThanEqual((testAt - relativeToChunk * chunkDim - farBoundaries) * dir_, ivec3(0,0,0));
			
			if(all(inBounds)) {
				bool next = false;
				
				for(int i = 0; i < 100; i++) {
					const float minCurLen = min(min(curLen.x, curLen.y), curLen.z);
					const bvec3 minAxis_b = equal(curLen, vec3(1,1,1) * minCurLen);
					const  vec3 minAxis_f = vec3(minAxis_b);
					const ivec3 minAxis_i = ivec3(minAxis_b);
					
					const  ivec3 otherAxis_i = ivec3(not(minAxis_b));
					const   vec3 otherAxis_f =  vec3(not(minAxis_b));
					const vec3 curCoordF = at(ray, minCurLen);
					const ivec3 curCoord = ivec3(floor(curCoordF));
					
					const ivec3 cellAt =  
							+   minAxis_i * (firstCellRow - negative_ + curSteps*dir_)
							+ otherAxis_i * curCoord;
							
					if(minCurLen > maxDist) return emptyOptional_BlockIntersection();
							
					const bvec3 inBounds = lessThanEqual((cellAt - relativeToChunk * chunkDim - farBoundaries) * dir_, ivec3(0,0,0));
					if( !all(inBounds) ) {
						next = true;
						break;
					}
					
					const ivec3 checks = ivec3( (equal(curCoordF, curCoord) || minAxis_b) );
					
					Optional_BlockIntersection glass = emptyOptional_BlockIntersection();
					
					allSkip = true;
					for(int x = checks.x; x >= 0; x --) {
						for(int y = checks.y; y >= 0; y --) {
							for(int z = checks.z; z >= 0; z --) {
								const ivec3 ca = cellAt - ivec3(x, y, z) * dir_;
								
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
									allSkip = false;
									
									const vec3 blockCoord = curCoordF - curCoord;
									vec2 uv = vec2(
										dot(minAxis_f, blockCoord.zxx),
										dot(minAxis_f, blockCoord.yzy)
									);
									const ivec3 side = minAxis_i * (ivec3(x, y, z) * 2 - 1) * dir_;
									const bool backside = dot(vec3(side), ray.dir) > 0;
									
									if(blockId == 5) {
										if(int(dot(ivec2(uv * vec2(4, 8)), vec2(1))) % 2 != 0) continue;
									}
									if(blockId == 7) {
										if(glass.is) { glass.it.glassDir = ray.dir; continue; }
										
										const vec3 glassOffset = vec3( 
											sin(blockCoord.x*9)/2, 
											sin(blockCoord.z*9)/8, 
											sin(blockCoord.y*6 + blockCoord.x*4)/8
										) / 100;
										
										const vec3 glassBlockCoord = blockCoord - glassOffset;
										
										vec2 glassUv = vec2(
											dot(minAxis_f, glassBlockCoord.zxx),
											dot(minAxis_f, glassBlockCoord.yzy)
										);
										const vec2 offset = atlasAt(blockId, side);
										const vec3 color = sampleAtlas(offset, clamp(glassUv, 0.0001, 0.9999));
	
										const vec3 incoming = ray.dir - glassOffset * vec3(1-checks);
										const vec3 refracted = refract(normalize(incoming), normalize(vec3(side)), 1.2);
										
										glass = Optional_BlockIntersection(
											true,    
											BlockIntersection(
												side,
												//backside,
												normalize(refracted),
												color,
												minAxis_f * (firstCellRow + curSteps*dir_ )
												+ otherAxis_f * curCoordF,
												minCurLen,
												curChunkIndex,
												blockId
											)
										);
										continue;
									}
									
									const vec2 offset = atlasAt(blockId, side);
									const vec3 color = sampleAtlas(offset, uv) * mix(vec3(1), glass.it.color, vec3(glass.is));
									
									return Optional_BlockIntersection(
										true,    
										BlockIntersection(
											//ca_,
											side,
											//backside,
											vec3(0),
											color,
											minAxis_f * (firstCellRow + curSteps*dir_ + ivec3(minAxis_b) * negative_)
											+ otherAxis_f * curCoordF,
											//uv,
											minCurLen,
											curChunkIndex,
											blockId
										)
									 );
								}
							}	
						}
					}
					
					if(allSkip) allSkipMinLen = min(allSkipMinLen, minCurLen);
					
					if(glass.is) return glass;
					
					curSteps += minAxis_i;
					curLen += minAxis_f * stepLength;
				}
				
				if(!next) break;
			}
		}
		
		const vec3 border0Dist  = -min(relativeOrig - chunkDim, vec3(0,0,0)) * dir_;
		const vec3 border16Dist = -max(relativeOrig, vec3(0,0,0)) * dir_;
		
		const vec3 maxBorderDiff = max(border0Dist, border16Dist);
		const ivec3 minSteps_vec = ivec3(floor(maxBorderDiff));
		const vec3 minLen_vec = maxBorderDiff*stepLength;
		
		const float minLen = min(min(minLen_vec.x, minLen_vec.y), minLen_vec.z); //minimal length for any ray to get outside of chunk bounds
		const bvec3 outside = greaterThan(minLen_vec, vec3(minLen));
		
		curSteps = 
				ivec3(not(outside)) * ivec3(minSteps_vec) +
				ivec3(   (outside)) * ivec3(max(ceil(minLen * abs(dir) - firstCellDiff),0));
		
		curLen = stepLength * curSteps + stepLength * firstCellDiff;
		
		const ivec3 neighbourDir = ivec3(!outside) * dir_;
		const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, neighbourDir);
		if(candChunkIndex < 0) break;
		
		curChunkIndex = candChunkIndex;
		relativeToChunk += neighbourDir;
		
	}

	return emptyOptional_BlockIntersection();
}

vec3 background(const vec3 dir) {
	const float t = 0.5 * (dir.y + 1.0);
	const vec3 res = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
	return pow(res, vec3(2.2));
}

struct Trace {
	vec3 color;
	float depth;
};

Trace trace(Ray ray, int chunkIndex) {
	vec3 col;
	float t;
	
	bool glass = false;
	vec3 glassColor = vec3(1);
	vec3 shadowGlassColor = vec3(1);
	
	bool shadow = false;
	vec3 origColor;
	
	for(int i = 0; i < 100; i ++) {
		const Optional_BlockIntersection intersection = isInters(ray, chunkIndex, far);
		
		if(intersection.is) {
			const BlockIntersection i = intersection.it;
			if(!glass && !shadow) t = i.t;
			const uint blockId = i.id;
			
			const int intersectionChunkIndex = i.chunkIndex;
			col = i.color;
			

			//if(true) return Trace(col, t);
			if(blockId == 7) {
				ray = Ray( i.at - (chunkPosition(intersectionChunkIndex) - chunkPosition(chunkIndex)) * chunkDim + i.glassDir * 0.001, i.glassDir );
				chunkIndex = intersectionChunkIndex;
				glass = true;
				
				if(shadow) shadowGlassColor = glassColor * i.color;
				else glassColor = glassColor * i.color;
			}
			else {
				if(shadow) {
					return Trace(origColor.xyz * 0.4 * mix(vec3(1), glassColor, vec3(glass)), t);
				}
				else// if(t < far/5)
				{
					ray = Ray( i.at - (chunkPosition(intersectionChunkIndex) - chunkPosition(chunkIndex)) * chunkDim + normalize(vec3(i.side)) * 0.0001, normalize(vec3(1,3,2)) );
					chunkIndex = intersectionChunkIndex;
					shadow = true;
					origColor = col;
				}
				//else {
				//	return Trace(col.xyz * mix(vec3(1), glassColor, vec3(glass)), t);
				//}
			}
		}
		else if(shadow) {
			return Trace(origColor.xyz * mix(vec3(1), glassColor * shadowGlassColor, vec3(glass)), t);
		}
		else if(glass) {
			return Trace(background(ray.dir) * mix(vec3(1), glassColor * shadowGlassColor, vec3(glass)), t);
		}
		else break;
	}
	
	return Trace(background(ray.dir), far);
}

void main() {
	const vec2 uv = gl_FragCoord.xy / windowSize.xy;
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const vec3 relativeChunkPos = relativeChunkPosition(startChunkIndex);
	const Ray ray = Ray(-relativeChunkPos, rayDir);
	
	
	const vec3 modelColor = texture2D(worldColor, uv).rgb;
	const float modelDepth = texture2D(worldDepth, uv).r;
	
	const float modelZ = 1.0 / (modelDepth * (1.0 / far - 1.0 / near) + 1.0 / near);
	const float modelProj = (modelZ - projection[3].z) / projection[2].z;
	const float modelDist = modelProj / dot(forwardDir, rayDir);
	
	float modelDistCorrected = modelDepth == 1 ? far : modelDist;
	
	
	const Trace trc = trace(ray, startChunkIndex);
	
	
	vec3 col = trc.color;
	const float t = clamp(trc.depth, near, far);
	if(t > modelDistCorrected) col = modelColor;
	

	if(length(gl_FragCoord.xy - windowSize / 2) < 3) color = vec4(vec3(0.98), 1);
	else color = vec4(col, 1);
}		