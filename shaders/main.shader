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

uniform sampler2D noise;

//copied from Chunks.h
#define chunkDim 16

layout(binding = 1) restrict readonly buffer ChunksIndices {
     uint data[][16*16*16];
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
	const uint id = data[chunkIndex][index];
	return id;
}

uint blockAt_unnormalized(int chunkIndex, ivec3 blockCoord) {
	const ivec3 outDir = testBounds(blockCoord);
	
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkNeighbourIndex(chunkIndex, outDir);
		if(candChunkIndex < 0) return 0;//-1;
		
		chunkIndex = candChunkIndex;
		blockCoord = blockCoord - outDir * chunkDim;
	}
	
	return blockAt(chunkIndex, blockCoord);
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
	vec3 normal;
	vec3 newDir;
	vec3 color;
	vec3 at;
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

//https://www.scratchapixel.com/lessons/3d-basic-rendering/introduction-to-shading/reflection-refraction-fresnel
vec3 reflect(const vec3 incoming, const vec3 normal)  { 
    return incoming - 2 * dot(incoming, normal) * normal; 
} 

//https://www.scratchapixel.com/lessons/3d-basic-rendering/introduction-to-shading/reflection-refraction-fresnel
vec3 refract2(const vec3 incoming, const vec3 normal, const float ior)  { 
    float cosi = clamp(-1, 1, dot(incoming, normal));
    float etai = 1, etat = ior; 
    vec3 n = normal; 
    if (cosi < 0) { cosi = -cosi; } else { float tmp = etai; etai = etat; etat = tmp; n= -normal; } 
    const float eta = etai / etat; 
    const float k = 1 - eta * eta * (1 - cosi * cosi); 
    return k < 0 ? reflect(incoming, normal) : eta * incoming + (eta * cosi - sqrt(k)) * n; 
}

vec3 screenMultipty(const vec3 v1, const vec3 v2) {
	return 1 - (1-v1) * (1-v2);
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

ivec3 mix3i(const ivec3 a, const ivec3 b, const ivec3 f) {
	return (1-f) * a + b * f;
}

int dot3i(const ivec3 a, const ivec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct BlockInfo {
	vec3 normal;
	vec3 newRayDir;
	vec3 color;
	uint block;
};

BlockInfo testBlocks(
	const Ray ray,
	const vec3 fromBlockCoord, const uint fromBlock, 
	const vec3 toBlockCoord, const uint toBlock, 
	const vec3 curCoordF, const ivec3 intersectionSide
) {
	const vec3 dir = ray.dir;
	const ivec3 dir_ = ivec3(sign(dir));
	
	const uint blocks[] = {
		fromBlock,
		toBlock
	};
	const vec3 blockCoords[] = {
		fromBlockCoord,
		toBlockCoord
	};
			
	BlockInfo glass = BlockInfo(vec3(0), vec3(0), vec3(1), 0);
	for(int s = 0; s < 2; s ++) {
		const uint blockId = blocks[s];
		const vec3 blockCoord = blockCoords[s];
		
		if(blockId != 0) { 
			const vec3 blockCoord = curCoordF - blockCoord;
			vec2 uv = clamp(vec2(
				dot(intersectionSide, blockCoord.zxx),
				dot(intersectionSide, blockCoord.yzy)
			), 0.0001, 0.9999);
			const ivec3 side = intersectionSide * ((1-s) * 2 - 1) * dir_;
			const bool backside = dot(vec3(side), ray.dir) > 0;
			const ivec3 normal = side * ( backside ? -1 : 1 );
			
			const vec2 offset = atlasAt(blockId, side);
			const vec3 color = sampleAtlas(offset, uv) * glass.color;
			
			if(blockId == 5) { //leaves
				if(int(dot(ivec2(uv * vec2(4, 8)), vec2(1))) % 2 != 0) continue; 
			}
			if(blockId == 7) { //glass
				if(glass.block != 0) { glass = BlockInfo(vec3(0), vec3(0), vec3(1), 0); continue; }
				
				const vec3 glassOffset = vec3( 
					sin(blockCoord.x*9)/2, 
					sin(blockCoord.z*9)/8, 
					sin(blockCoord.y*6 + blockCoord.x*4)/8
				) / 100;
				
				const vec3 glassBlockCoord = blockCoord - glassOffset;
				
				vec2 glassUv = vec2(
					dot(intersectionSide, glassBlockCoord.zxx),
					dot(intersectionSide, glassBlockCoord.yzy)
				);
				const vec2 offset = atlasAt(blockId, side);
				const vec3 color = sampleAtlas(offset, clamp(glassUv, 0.0001, 0.9999));
	
				const vec3 incoming = ray.dir - glassOffset * vec3(1-intersectionSide);
				const vec3 refracted = refract2(normalize(incoming), normalize(vec3(side)), 1.1 );
				
				glass = BlockInfo(
					normal,
					normalize(refracted),
					color,
					blockId
				);
				continue;
			}
			if(blockId == 8) { //diamond						
				const vec3 offset = normalize(texture2D(noise, uv/5).xyz - 0.5) * 0.01;
				const vec3 reflected_ = reflect( ray.dir, normalize( normal + offset ) );
				const vec3 reflected  = abs(reflected_) * normal + reflected_ * (1 - abs(normal));
				
				return BlockInfo(
					normal,
					reflected,
					color,
					blockId
				);
			}
			
			//other blocks
			return BlockInfo(
				normal,
				vec3(0),
				color,
				blockId
			);
		}
	}	
		
	return glass; //glass may have block id = 0 - empty block
}

Optional_BlockIntersection isInters(const Ray ray, const int chunkIndex) { 	
	const vec3 dir = ray.dir;
	const ivec3 dir_ = ivec3(sign(dir));
	const ivec3 positive_ = max(+dir_, ivec3(0,0,0));
	const ivec3 negative_ = max(-dir_, ivec3(0,0,0));
	
	const vec3 stepLength = 1 / abs(dir);
	
	const  vec3 firstCellRow_f = vec3(positive_) * (floor(ray.orig)+1) + vec3(negative_) * (ceil(ray.orig)-1);
	const ivec3 firstCellRow   = ivec3(firstCellRow_f); 
	const  vec3 firstCellDiff  = abs(ray.orig-firstCellRow_f); //distance to the frist block side
	
	int curChunkIndex = chunkIndex;
	ivec3 relativeToChunk = ivec3(0);
	
	ivec3 lastSteps = ivec3(0);
	//uint lastBlock = blockAt_unnormalized(...);
	
	float prevMinLen = 0;
	while(true) {
		const uint  bounds = chunkBounds(curChunkIndex);
		const ivec3 startBorder = start(bounds);
		const ivec3 endBorder = onePastEnd(bounds);
		const bool  empty = emptyBounds(startBorder, endBorder);
		const ivec3 nearBoundaries = mix3i(endBorder, startBorder, positive_);
		const ivec3 farBoundaries = mix3i(startBorder, endBorder, positive_);
		
		const vec3  relativeOrig = ray.orig - relativeToChunk * chunkDim;
		  
		//calculate next chunk
		  const vec3 border16Dist  = -min(relativeOrig - chunkDim, vec3(0,0,0)) * dir_;
		  const vec3 border0Dist = -max(relativeOrig, vec3(0,0,0)) * dir_;
		  
		  const vec3 maxOutBorderDiff = max(border0Dist, border16Dist);
		  const ivec3 minOutSteps_vec = ivec3(floor(maxOutBorderDiff));
		  const vec3 minOutLen_vec = maxOutBorderDiff*stepLength;
		  
		  const float minOutLen = min(min(minOutLen_vec.x, minOutLen_vec.y), minOutLen_vec.z); //minimum length for any ray to get outside of chunk bounds
		  const bvec3 outOutside = greaterThan(minOutLen_vec, vec3(minOutLen));
		  const ivec3 firstToReachFarChunkBorder = ivec3(!outOutside);
		  
		  const ivec3 outNeighbourDir = firstToReachFarChunkBorder * dir_;
		  const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, outNeighbourDir);
		  //
		  const int nextChunkIndex = candChunkIndex;
		  const ivec3 nextRelativeToChunk = relativeToChunk + outNeighbourDir;

		
		if(!empty) {
			//find starting position for current chunk
			  const vec3 borderStartDist = max(startBorder - relativeOrig, vec3(0,0,0)) * dir_;
			  const vec3 borderEndDist   = max(relativeOrig - endBorder, vec3(0,0,0)) * (-dir_);
			  
			  const vec3 maxBorderDiff = max(borderStartDist, borderEndDist);
			  const ivec3 minSteps_vec = ivec3(floor(maxBorderDiff));
			  const vec3 minLen_vec = maxBorderDiff*stepLength;
			  
			  const float minLen = max(max(minLen_vec.x, minLen_vec.y), minLen_vec.z); //minimum length for all rays to get inside of chunk bounds
			  const bvec3 outside = lessThan(minLen_vec, vec3(minLen));
			  //
			  ivec3 curSteps = max(lastSteps, 
			    ivec3(not(outside)) * ivec3(minSteps_vec) +
			    ivec3(   (outside)) * ivec3(max(ceil(minLen * abs(dir) - firstCellDiff),0)));
			  	
			  vec3 curLen = stepLength * curSteps + stepLength * firstCellDiff;
			
			//test if calculated starting position is in chunk boundaries
			  const float testMinCurLen = min(min(curLen.x, curLen.y), curLen.z);
			  const bvec3 testMinAxis_b = equal(curLen, vec3(testMinCurLen));
			  const  vec3 testMinAxis_f = vec3(testMinAxis_b);
			  const ivec3 testMinAxis_i = ivec3(testMinAxis_b);
			  
			  const ivec3 testOtherAxis_i = ivec3(not(testMinAxis_b));
			  const  vec3 testOtherAxis_f =  vec3(not(testMinAxis_b));
			  const  vec3 testCoordF = at(ray, testMinCurLen);
			  
			  const  vec3 testCoordAt = testMinAxis_f * (firstCellRow + curSteps*dir_) + testOtherAxis_f * testCoordF;
			  const ivec3 testBlockAt = ivec3(floor(testCoordAt)) - testMinAxis_i * negative_;

			
			const bvec3 inChunkBounds = lessThanEqual((testCoordAt - relativeToChunk * chunkDim - farBoundaries) * dir_, vec3(0,0,0));
			if(all(inChunkBounds)) {
				//if(any(notEqual(nearBoundaries, mix3i(ivec3(chunkDim), ivec3(0), positive_)) && !outside)) lastBlock = 0;
				uint fromBlock; 
				{
					const ivec3 checks = ivec3( (equal(testCoordF, floor(testCoordF)) || testMinAxis_b) );
					
					fromBlock = blockAt_unnormalized(
						curChunkIndex,
						testBlockAt - checks*dir_ - relativeToChunk * chunkDim
					);
				}
			
				while(true) {
					const float minCurLen   = min(min(curLen.x, curLen.y), curLen.z);
					const bvec3 minAxis_b   = equal(curLen, vec3(minCurLen));
					const  vec3 minAxis_f   = vec3(minAxis_b);
					const ivec3 minAxis_i   = ivec3(minAxis_b);
					
					const bvec3 otherAxis_b = not(minAxis_b);
					const ivec3 otherAxis_i = ivec3(not(minAxis_b));
					const  vec3 otherAxis_f =  vec3(not(minAxis_b));
					const  vec3 curCoordF   = at(ray, minCurLen);
					const ivec3 curCoord = ivec3(floor(curCoordF));
					//
					const vec3 coordAt = 
							+   minAxis_f * (firstCellRow + curSteps*dir_)
							+ otherAxis_f * curCoordF;
					const ivec3 cellAt = ivec3(floor(coordAt - minAxis_f * negative_));
							
					const bool inBounds = all(
						lessThan((firstCellRow + curSteps*dir_ - relativeToChunk * chunkDim - farBoundaries) * dir_, ivec3(0,0,0)) 
						|| otherAxis_b
					);
					
					const ivec3 faces = ivec3(minAxis_b);
					
					const ivec3 fromBlockCoord = cellAt - faces*dir_;
					const ivec3 toBlockCoord = cellAt;
					
					
					uint toBlock;
					if(checkBoundaries(toBlockCoord - relativeToChunk * chunkDim)) toBlock = blockAt(
						curChunkIndex,
						toBlockCoord - relativeToChunk * chunkDim
					);
					else toBlock = blockAt(
						nextChunkIndex,
						toBlockCoord - nextRelativeToChunk * chunkDim
					);					
					
					const BlockInfo i = testBlocks(
						ray,
						fromBlockCoord, fromBlock, 
						toBlockCoord  , toBlock  ,
						coordAt, minAxis_i
					);
					fromBlock = toBlock;
					
					
					if(i.block != 0) return Optional_BlockIntersection(
						true,
						BlockIntersection(
							i.normal,
							i.newRayDir,
							i.color,
							coordAt,
							minCurLen,
							curChunkIndex,
							i.block
						)
					);
					
					curSteps += minAxis_i;
					prevMinLen = minCurLen;
					curLen += minAxis_f * stepLength;
					
					if(!inBounds) {
						lastSteps = curSteps;
						break;
					}
				}
			}
		}
		
		if(nextChunkIndex < 0) break;
		
		//if(empty || any(notEqual(farBoundaries, mix3i(ivec3(0), ivec3(chunkDim), positive_)) && !outOutside)) lastBlock = 0;
		
		//update current chunk
		curChunkIndex = nextChunkIndex;
		relativeToChunk = nextRelativeToChunk;	
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

struct Step {
	vec3 color;
	float depth;
	uint block;
};

Step combineSteps(const Step current, const Step previous) {
	if(current.block == 0) return current;
	else if(current.block == 7) return Step(previous.color * current.color, current.depth, current.block);
	else if(current.block == 8) return Step(previous.color * current.color, current.depth, current.block);
	else return current;
}

Trace trace(Ray ray, int chunkIndex) {
	const int maxSteps = 10;
	Step steps[maxSteps];
	
	bool shadow = false;
	int shadowIndex;
	int curSteps = 0;
	
	while(true) {
		if(curSteps == maxSteps) { curSteps--; break; }
		
		int chunkCount;
		Optional_BlockIntersection intersection = isInters(ray, chunkIndex);
		
		if(intersection.is) {
			const BlockIntersection i = intersection.it;
			const float t = i.t;
			const uint blockId = i.id;
			
			const int intersectionChunkIndex = i.chunkIndex;
			const vec3 col = i.color;
			const vec3 at = i.at;
			
			if(blockId == 7) {
				ray = Ray( i.at - (chunkPosition(intersectionChunkIndex) - chunkPosition(chunkIndex)) * chunkDim, i.newDir );
				chunkIndex = intersectionChunkIndex;
				
				steps[curSteps++] = Step( col, t, blockId );
				continue;
			}
			if(blockId == 8) {
				ray = Ray( i.at - (chunkPosition(intersectionChunkIndex) - chunkPosition(chunkIndex)) * chunkDim, i.newDir );
				chunkIndex = intersectionChunkIndex;
				
				steps[curSteps++] = Step( col, t, blockId );
				continue;
			}
			
			{
				if(shadow) {
					steps[curSteps] = Step( col, t, blockId );
					break;
				}
				shadow = true;
				
				const vec3 startRelativeAt = at - (chunkPosition(intersectionChunkIndex) - chunkPosition(chunkIndex)) * chunkDim;
				
				const int shadowOffsetsCount = 3;
				const float shadowOffsets[] = { -1, 0, 1 };
				
				const float shadowSubdiv = 32;
				const float shadowSmoothness = 32;
				
				const vec3 offset_ = vec3(
					shadowOffsets[ int(mod(floor(startRelativeAt.x * shadowSubdiv), shadowOffsetsCount)) ],
					shadowOffsets[ int(mod(floor(startRelativeAt.y * shadowSubdiv), shadowOffsetsCount)) ],
					shadowOffsets[ int(mod(floor(startRelativeAt.z * shadowSubdiv), shadowOffsetsCount)) ]
				);
				const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness;
				
				const vec4 q = vec4(normalize(vec3(1+sin(time)/10,3,2)), cos(time)/5);
				const vec3 v = normalize(vec3(1, 4, 2) + offset);
				const vec3 temp = cross(q.xyz, v) + q.w * v;
				const vec3 rotated = v + 2.0*cross(q.xyz, temp);
				
				const vec3 dir = normalize(rotated);
				ray = Ray( floor( startRelativeAt * shadowSubdiv)/shadowSubdiv + normalize(i.normal) * 0.0001, dir );
				chunkIndex = intersectionChunkIndex;
				
				shadowIndex = curSteps++;
				steps[shadowIndex] = Step( col, t, blockId );
				continue;
			}
		}
		else {
			steps[curSteps] = Step( background(ray.dir), far, 0 );
			break;
		}
	}
	
	const Step last = steps[curSteps--];
	
	Step previous = last;
	
	if(shadow) {
		if(last.block == 0) {
			previous = Step(vec3(1), 1, 0);
			for(; curSteps > shadowIndex; curSteps--) {
				const Step current = steps[curSteps];
				previous = combineSteps(current, previous);
			}
			//curSteps == shadowIndex
			const Step current = steps[curSteps--]; 
			previous = Step( current.color * previous.color, current.depth, current.block );
		}
		else {
			curSteps = shadowIndex;
			const Step current = steps[curSteps--]; 
			previous = Step( current.color * 0.4, current.depth, current.block );
		}
	}
	else if(last.block != 0) previous = Step( vec3(0), 1, 0 );
	
	for(; curSteps >= 0; curSteps--) {
		const Step current = steps[curSteps];
		previous = combineSteps(current, previous);
	}
	
	return Trace(previous.color, previous.depth);
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
	else 
		color = vec4(col, 1);
}		