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

uniform sampler2D noise;

uniform vec3 playerRelativePosition;
uniform bool drawPlayer;

//copied from Chunks.h
#define chunkDim 16

layout(binding = 1) restrict readonly buffer ChunksBlocks {
     uint data[][16*16*16];
} chunksBlocks;

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
bool emptyBounds(const int chunkIndex) {
	if(chunkIndex < 0) return true;
	const uint bounds = chunkBounds(chunkIndex);
	return emptyBounds(start(bounds), onePastEnd(bounds));
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

bool chunkNotLoaded(const int chunkIndex) {
	if(chunkIndex == -1) return true;
	const int index = chunkIndex * neighboursCount;
	for(int i = 0; i < neighboursCount; i ++) 
		if(ns.neighbours[index + i] != 0) return false;
	return true;
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

ivec3 shr3i(const ivec3 v, const int i) {
	return ivec3(v.x >> i, v.y >> i, v.z >> i);
}
ivec3 and3i(const ivec3 v, const int i) {
	return ivec3(v.x & i, v.y & i, v.z & i);
}

vec3 relativeChunkPosition(const int chunkIndex) {
	return vec3( (chunkPosition(chunkIndex) - playerChunk) * chunkDim ) - playerInChunk;
}


vec3 sampleAtlas(const vec2 offset, const vec2 coord) {
    vec2 uv = vec2(
        coord.x + offset.x,
        coord.y + atlasTileCount.y - (offset.y + 1)
    ) / atlasTileCount;
    return pow(texture2D(atlas, uv).rgb, vec3(2.2));
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

//copied from Chunks::Block
struct Block {
	uint data;
};
  uint blockId(const Block block) {
  	return block.data & ((1 << 16) - 1);
  }
  uint blockCubes(const Block block) {
  	return (block.data >> 24) /* & 255*/; 
  }
  bool cubeAt(const uint cubes, const ivec3 upperCube) { 
  	const int index = upperCube.x + upperCube.y * 2 + upperCube.z * 4;
  	return bool( (cubes >> index) & 1 );
  }
  bool fullBlock(const uint cubes) {
	  return cubes == 255;
  }

Block blockAir() {
	return Block(0);
}

Block blockAt(const int chunkIndex, const ivec3 i_v) {
	const int index = i_v.x + i_v.y * 16 + i_v.z * 16 * 16;
	const uint blockData = chunksBlocks.data[chunkIndex][index];
	return Block(blockData);
}

Block blockAt_unnormalized(int chunkIndex, ivec3 blockCoord) {
	const ivec3 outDir = testBounds(blockCoord);
	
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkNeighbourIndex(chunkIndex, outDir);
		if(candChunkIndex < 0) return blockAir();
		
		chunkIndex = candChunkIndex;
		blockCoord = blockCoord - outDir * chunkDim;
	}
	
	return blockAt(chunkIndex, blockCoord);
}

layout(binding = 6) restrict readonly buffer ChunksAO {
    uint data[];
} ao;

//copied from Chunks.h
  ivec3 vertCoord(const int index) {
  	return ivec3( index % (chunkDim*2), (index / (chunkDim*2)) % (chunkDim*2), (index / (chunkDim*2) / (chunkDim*2)) );
  }
  
  int coordVert(const ivec3 coord) {
  	return coord.x + coord.y*(chunkDim*2) + coord.z*(chunkDim*2)*(chunkDim*2);
  }
  
uint aoAt(const int chunkIndex, const ivec3 vertexCoord) {
	const int startIndex = chunkIndex * chunkDim*2*chunkDim*2*chunkDim*2;
	const int offset = coordVert(vertexCoord);
	const int index = startIndex + offset;
	
	const int el = index / 4;
	const int sh = (index % 4) * 8;
	
	return (ao.data[el] >> sh) & 255;
}
  
  ivec3 vertexNeighbourOffset(const int index) {
  	const int x = int((index % 2)       != 0); //0, 2, 4, 6 - 0; 1, 3, 5, 7 - 1
  	const int y = int(((index / 2) % 2) != 0); //0, 1, 4, 5 - 0; 2, 3, 6, 7 - 1
  	const int z = int((index / 4)       != 0); //0, 1, 2, 3 - 0; 4, 5, 6, 7 - 1
  	return ivec3(x,y,z);
  }

float lightForChunkVertexDir(const int chunkIndex, const ivec3 vertex, const ivec3 dir) {
	const bvec3 dirMask = notEqual(dir, ivec3(0));
	const int dirCount = int(dirMask.x) + int(dirMask.y) + int(dirMask.z);
	if(dirCount == 0) return 0;
	const float maxLight = dirCount == 1 ? 4 : (dirCount == 2 ? 6 : 7);
	
	int curChunkIndex = chunkIndex;
	ivec3 cubeCoord = vertex;
	
	const ivec3 outDir = testBounds(shr3i(cubeCoord, 1));
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkNeighbourIndex(chunkIndex, outDir);
		if(candChunkIndex < 0) return 1;
		
		curChunkIndex = candChunkIndex;
		cubeCoord -= outDir * chunkDim*2;
	}
	
	const uint vertexAO = aoAt(curChunkIndex, cubeCoord);
	
	int blocks = 0;
	for(int i = 0; i < 8; i++) {
		const ivec3 vertexDirs = vertexNeighbourOffset(i);
		if(any(equal(vertexDirs*2-1, dir))) {
			blocks += int( (vertexAO >> i) & 1 );
		}
	}
	
	return (maxLight - float(blocks)) / maxLight;
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

struct Surface {
	uint data;
};

uint surfaceType(const Surface s) {
	return s.data & ~(1u << 31);
}

bool isSurfaceBlock(const Surface s) {
	return (s.data >> 31) == 0;
}

bool isSurfaceEntity(const Surface s) {
	return (s.data >> 31) != 0;
}

bool isNoSurface(const Surface s) {
	return surfaceType(s) == 0;
}

Surface noSurface() {
	return Surface(0);
}

Surface surfaceBlock(const uint type) {
	return Surface(type);
}

Surface surfaceEntity(const uint type) {
	return Surface(type | (1u << 31));
}

struct BlockIntersection {
	vec3 normal;
	vec3 newDir;
	vec3 color;
	vec3 at;
	float t;
	int chunkIndex;
	Surface surface;
	float ao;
};

BlockIntersection noBlockIntersection() {
	BlockIntersection i;
	i.surface = noSurface();
	return i;
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

float intersectPlane(const Ray r, const vec3 center, const vec3 n) {
    return dot(n, center - r.orig) / dot(n, r.dir);
}

float intersectSquare(const Ray r, const vec3 center, const vec3 n, const vec3 up, const vec3 left, inout vec2 uv) {
    const vec3 c = center + n; //plane center
    const float t = intersectPlane(r, c, n);
    const vec3 p = at(r, t); //point
    vec3 l = p - c; //point local to plane
    float u_ = dot(l, up);
    float v_ = dot(l, left);
    if (abs(u_) <= 1 && abs(v_) <= 1) {
        uv = (vec2(u_, v_) / 2 + 0.5);
        return t;
    }
    return 1.0 / 0.0;
}

struct Intersection {
	vec3 normal;
	vec3 newRayDir;
	vec3 color;
	vec3 at;
	float t;
	Surface surface;
	bool frontface;
};
//float intersectCube(const Ray ray, const vec3 center, const vec3 n1, const vec3 n2, const vec3 size, out vec3 n_out, out vec2 uv_out) {
Intersection intersectCube(const Ray ray, const vec3 start, const vec3 end, const vec3 n1, const vec3 n2) {
	const vec3 size = (end - start) / 2;
	const vec3 center = (end + start) / 2; 
	const int frontface = int(all(equal(ray.orig, clamp(ray.orig, start, end)))) * -2 + 1;
    const vec3 n3 = cross(n2, n1);

	const vec3 nn[3] = { 
         n1 * -sign( dot(n1, ray.dir) ) * frontface
        ,n2 * -sign( dot(n2, ray.dir) ) * frontface
        ,n3 * -sign( dot(n3, ray.dir) ) * frontface
    };
	
    const vec3 ns[3] = { 
         nn[0] / size.x
        ,nn[1] / size.y
        ,nn[2] / size.z
    };

    vec2 uvs[3];

    const uint sides = 3;
    const float arr[sides] = {
         intersectSquare(ray, center, nn[0] * size.x, ns[2], ns[1], uvs[0])
        ,intersectSquare(ray, center, nn[1] * size.y, ns[0], ns[2], uvs[1])
        ,intersectSquare(ray, center, nn[2] * size.z, ns[0], ns[1], uvs[2])
    };

    float shortestT = 1.0 / 0.0;
	vec3 normal;
	//vec2 uv;
	
    for (uint i = 0; i < sides; i++) {
        float t = arr[i];
        if (t >= 0 && t < shortestT) {
            shortestT = t;
            normal = nn[i];
            //uv = uvs[i];
        }
    }

	return Intersection(
		normal,
		vec3(0),
		vec3(1),
		mix(center + normal * size * frontface, at(ray, shortestT), equal(normal, vec3(0))),
		shortestT,
		surfaceEntity(1),
		frontface > 0
	);
}

struct BlockInfo {
	ivec3 normal;
	vec3 newRayDir;
	vec3 color;
	uint block;
	bool frontface;
};

BlockInfo testFace(
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
			
	BlockInfo glass = BlockInfo(ivec3(0), vec3(0), vec3(1), 0, false);
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
				if(glass.block != 0) { glass = BlockInfo(ivec3(0), vec3(0), vec3(1), 0, false); continue; }
				
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
					blockId,
					!backside
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
					blockId,
					!backside
				);
			}
			
			//other blocks
			return BlockInfo(
				normal,
				vec3(0),
				color,
				blockId,
				!backside
			);
		}
	}	
		
	return glass; //glass may have block id = 0 - empty block
}

BlockIntersection isInters(const Ray ray, ivec3 relativeToChunk, int curChunkIndex, const bool drawPlayer) { 	
	const vec3 dir = ray.dir;
	const ivec3 dir_ = ivec3(sign(dir));
	const ivec3 positive_ = max(+dir_, ivec3(0,0,0));
	const ivec3 negative_ = max(-dir_, ivec3(0,0,0));
	
	const vec3 stepLength = 1 / abs(dir);
	
	const  vec3 firstCellRow_f = vec3(positive_) * (floor(ray.orig)+1) + vec3(negative_) * (ceil(ray.orig)-1);
	const ivec3 firstCellRow   = ivec3(firstCellRow_f); 
	const  vec3 firstCellDiff  = abs(ray.orig-firstCellRow_f); //distance to the frist block side
	
	ivec3 lastSteps = ivec3(0);
	//uint lastBlock = blockAt_unnormalized(...);
	
	const vec3 startP = playerRelativePosition - (vec3(0.3, 1.95/2, 0.3) - 0.001);
	const vec3 endP   = playerRelativePosition + (vec3(0.3, 1.95/2, 0.3) - 0.001);
	
	const Intersection playerIntersection = intersectCube(ray, startP, endP, vec3(1,0,0), vec3(0,1,0));
	
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
		  const vec3 maxOutBorderDiff = mix(relativeOrig, chunkDim - relativeOrig, positive_);//max(border0Dist, border16Dist);
		  const vec3 minOutLen_vec = maxOutBorderDiff*stepLength;
		  
		  const float minOutLen = min(min(minOutLen_vec.x, minOutLen_vec.y), minOutLen_vec.z); //minimum length for any ray to get outside of chunk bounds
		  const bvec3 firstOut = equal(minOutLen_vec, vec3(minOutLen));

		  const ivec3 outNeighbourDir = ivec3(firstOut) * dir_;
		  const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, outNeighbourDir);
		  //
		  const int nextChunkIndex = candChunkIndex;
		  const ivec3 nextRelativeToChunk = relativeToChunk + outNeighbourDir;
		  const bool nextNotLoaded = chunkNotLoaded(nextChunkIndex);

		
		if(!empty) {
			//find starting position for current chunk
			  const vec3 maxBorderDiff = max(mix(relativeOrig - endBorder, startBorder - relativeOrig, positive_), 0);//max(borderStartDist, borderEndDist);
			  const ivec3 minSteps_vec = ivec3(floor(maxBorderDiff));
			  const vec3 minLen_vec = maxBorderDiff*stepLength;
			  
			  const float minLen = max(max(minLen_vec.x, minLen_vec.y), minLen_vec.z); //minimum length for all rays to get inside of chunk bounds
			  const bvec3 outside = lessThan(minLen_vec, vec3(minLen));
			  //
			  ivec3 curSteps = max(lastSteps, 
			    ivec3(not(outside)) * ivec3(minSteps_vec) +
			    ivec3(   (outside)) * ivec3(max(ceil(minLen * abs(dir) - firstCellDiff),0)));
			  	
			//test if calculated starting position is in chunk boundaries
			  const ivec3 testMinAxis = ivec3(not(outside));
			  const ivec3 testOtherAxis = ivec3(outside);
			  
			  const  vec3 testCoordF = at(ray, minLen);
			  
			  const  vec3 testCoordAt = testMinAxis * (firstCellRow + curSteps*dir_) + testOtherAxis * testCoordF;
			  const ivec3 testBlockAt = ivec3(floor(testCoordAt)) - testMinAxis * negative_;

			
			const bvec3 inChunkBounds = lessThanEqual((testCoordAt - relativeToChunk * chunkDim - farBoundaries) * dir_, vec3(0,0,0));
			if(all(inChunkBounds)) {
				//if(any(notEqual(nearBoundaries, mix3i(ivec3(chunkDim), ivec3(0), positive_)) && !outside)) lastBlock = 0;
				Block fromBlock; 
				{
					const ivec3 checks = ivec3(testMinAxis);
					fromBlock = blockAt_unnormalized(
						curChunkIndex,
						testBlockAt - checks*dir_ - relativeToChunk * chunkDim
					);
				}
			
				while(true) {
					const vec3 curLen = stepLength * curSteps + stepLength * firstCellDiff;
					
					const float curMinLen   = min(min(curLen.x, curLen.y), curLen.z);
					const bvec3 minAxis_b   = equal(curLen, vec3(curMinLen));
					const ivec3 minAxis     = ivec3(minAxis_b);
					
					const bvec3 otherAxis_b = not(minAxis_b);
					const ivec3 otherAxis = ivec3(not(minAxis_b));
					
					const  vec3 curCoordF   = at(ray, curMinLen);
					//
					const vec3 coordAt =  minAxis * (firstCellRow + curSteps*dir_) + otherAxis * curCoordF;
					const ivec3 cellAt = ivec3(floor(coordAt - minAxis * negative_));
							
					const bool inBounds = all(
						lessThan((firstCellRow + curSteps*dir_ - relativeToChunk * chunkDim - farBoundaries) * dir_, ivec3(0,0,0)) 
						|| otherAxis_b
					);
					
					const ivec3 fromBlockCoord = cellAt - minAxis*dir_;
					const ivec3 toBlockCoord = cellAt;
					
					
					Block toBlock;
					if(checkBoundaries(toBlockCoord - relativeToChunk * chunkDim)) toBlock = blockAt(
						curChunkIndex,
						toBlockCoord - relativeToChunk * chunkDim
					);
					else if(nextNotLoaded) toBlock = blockAir();
					else toBlock = blockAt(
						nextChunkIndex,
						toBlockCoord - nextRelativeToChunk * chunkDim
					);	
					
					const uint fromBlockCubes = blockCubes(fromBlock);
					const uint    fromBlockId = blockId   (fromBlock);
					const uint   toBlockCubes = blockCubes(  toBlock);
					const uint      toBlockId = blockId   (  toBlock);
					
					if(fromBlockId != 0 || toBlockId != 0) {
						vec3 steps = (fromBlockCoord + negative_ - firstCellRow)*dir_+0.5;
						
						if(fromBlockId == 0 || fullBlock(fromBlockCubes)) steps = curSteps;
						
						for(int i = 0; i < 4; i++) {					
							const vec3  lengths = steps * stepLength + firstCellDiff * stepLength;
							const float currentLength = min(lengths.x, min(lengths.y, lengths.z));
							const bvec3 cubesMinAxisB = equal(lengths, vec3(currentLength));
							const ivec3 cubesMinAxis   = ivec3(    cubesMinAxisB );
							const ivec3 cubesOtherAxis = ivec3(not(cubesMinAxisB)); 
							
							const bool last = any(equal(steps, vec3(curSteps)) && cubesMinAxisB);
							
							if(currentLength >= prevMinLen) {
								const vec3 coordF = at(ray, currentLength);
								const vec3 cubesCoordAt = cubesMinAxis * (firstCellRow + steps*dir_) + cubesOtherAxis * coordF;
								const vec3 localCubesCoordAt = cubesCoordAt - fromBlockCoord;
								
								const ivec3 fUpperCubes = ivec3(mod(localCubesCoordAt*2 - positive_ * cubesMinAxis, 2));
								const ivec3 sUpperCubes = ivec3(mod(localCubesCoordAt*2 - negative_ * cubesMinAxis, 2));
								
								const  uint sBlockCubes = last ? toBlockCubes : fromBlockCubes;
								const  uint sBlockId    = last ? toBlockId    : fromBlockId   ;
								const ivec3 sBlockCoord = last ? toBlockCoord : fromBlockCoord;
								
								const bvec2 cubes = bvec2(
									cubeAt(fromBlockCubes, fUpperCubes),
									cubeAt(sBlockCubes   , sUpperCubes)
								);
								
								const uint fBlockUsedId = cubes.x ? fromBlockId : 0;
								const uint sBlockUsedId = cubes.y ? sBlockId    : 0;
								
								if( (!(all(cubes) || all(not(cubes))) || last)) {
									const BlockInfo info = testFace(
										ray,
										fromBlockCoord, fBlockUsedId,
										sBlockCoord   , sBlockUsedId,
										cubesCoordAt, cubesMinAxis
									);
									
									if(info.block != 0) {
										if(drawPlayer && !isNoSurface(playerIntersection.surface) && currentLength >= playerIntersection.t) return BlockIntersection(
											playerIntersection.normal,
											playerIntersection.newRayDir,
											playerIntersection.color,
											playerIntersection.at,
											playerIntersection.t,
											curChunkIndex,
											playerIntersection.surface,
											1
										);
										else {
											const ivec3 otherAxis1 = cubesMinAxisB.x ? ivec3(0,1,0) : ivec3(1,0,0);
											const ivec3 otherAxis2 = cubesMinAxisB.z ? ivec3(0,1,0) : ivec3(0,0,1);
											const ivec3 vertexCoord = ivec3(floor(cubesCoordAt * 2)) - relativeToChunk * chunkDim * 2;
											
											float light = 0;
											if(info.frontface) {
												for(int i = 0 ; i < 4; i++) {
													const ivec2 offset_ = ivec2(i%2, i/2);
													const ivec3 offset = offset_.x * otherAxis1 + offset_.y * otherAxis2;
													const ivec3 curVertexCoord = vertexCoord + offset;
																		
													const float vertexLight = lightForChunkVertexDir(curChunkIndex, curVertexCoord, info.normal);
													
													const float diffX = abs(offset_.x - mod(dot(localCubesCoordAt, otherAxis1)*2, 1));
													const float diffY = abs(offset_.y - mod(dot(localCubesCoordAt, otherAxis2)*2, 1));
													light += mix(mix(vertexLight, 0, diffY), 0, diffX);
												}
											}
											else light = 0.5;
											
											return BlockIntersection(
												info.normal,
												info.newRayDir,
												info.color,
												cubesCoordAt,
												currentLength,
												curChunkIndex,
												surfaceBlock(info.block),
												light
											);
										}
									}
								}
							}				
							
							if(last) break; 
							
							steps += vec3(cubesMinAxis) / 2.0;
						}
					}
					
					fromBlock = toBlock;
					curSteps += minAxis;
					prevMinLen = curMinLen;
					
					lastSteps = curSteps;
					
					if(!inBounds) break;
				}
			}
		}
		
		
		//if(empty || any(notEqual(farBoundaries, mix3i(ivec3(0), ivec3(chunkDim), positive_)) && !outOutside)) lastBlock = 0;
		
		//update current chunk
		
		if(drawPlayer && !isNoSurface(playerIntersection.surface) && minOutLen >= playerIntersection.t) return BlockIntersection(
			playerIntersection.normal,
			playerIntersection.newRayDir,
			playerIntersection.color,
			playerIntersection.at,
			playerIntersection.t,
			curChunkIndex,
			playerIntersection.surface,
			1
		);
		
		if(nextChunkIndex < 0 || nextNotLoaded) break;
		
		curChunkIndex = nextChunkIndex;
		relativeToChunk = nextRelativeToChunk;	
	}

	return noBlockIntersection();
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
	Surface surface;
};

Step combineSteps(const Step current, const Step previous) {
	const uint type = surfaceType(current.surface);
	if(type == 0) return current;
	else if(type == 7) return Step(previous.color * current.color, current.depth, current.surface);
	else if(type == 8) return Step(previous.color * current.color, current.depth, current.surface);
	else return current;
}

Trace trace(Ray ray, int chunkIndex) {
	const int maxSteps = 10;
	Step steps[maxSteps];
	
	bool shadow = false;
	int shadowIndex;
	int curSteps = 0;
	
	const ivec3 startChunkPosition = chunkPosition(chunkIndex);
	
	while(true) {
		if(curSteps == maxSteps) { curSteps--; break; }
		
		const BlockIntersection i = isInters(ray, chunkPosition(chunkIndex) - startChunkPosition, chunkIndex, drawPlayer || curSteps != 0);
		
		if(!isNoSurface(i.surface)) {
			const float t = i.t;
			const uint blockId = surfaceType(i.surface);
			
			const int intersectionChunkIndex = i.chunkIndex;
			const vec3 col = i.color;
			const vec3 at = i.at;
			
			const float light = i.ao;
			
			if(blockId == 7) {
				ray = Ray( i.at - 0 + i.newDir * 0.0001, i.newDir );
				chunkIndex = intersectionChunkIndex;
				
				steps[curSteps++] = Step( col * mix(0.7, 1.0, pow(light, 2)), t, surfaceBlock(blockId) );
				continue;
			}
			if(blockId == 8) {
				ray = Ray( i.at - 0 + i.newDir * 0.0001, i.newDir );
				chunkIndex = intersectionChunkIndex;
				
				steps[curSteps++] = Step( col * mix(0.45, 1.0, pow(light, 2)), t, surfaceBlock(blockId) );
				continue;
			}
			
			{
				if(shadow) {
					steps[curSteps] = Step( col, t, surfaceBlock(blockId) );
			
					break;
				}
				shadow = true;
				
				const vec3 startRelativeAt = at - 0;
				
				const int shadowOffsetsCount = 4;
				const float shadowOffsets[] = { -1, 0, 1, 0 };
				
				const float shadowSubdiv = 32;
				const float shadowSmoothness = 32;
				
				const vec3 offset_ = vec3(
					shadowOffsets[ int(mod(floor(startRelativeAt.x * shadowSubdiv), shadowOffsetsCount)) ],
					shadowOffsets[ int(mod(floor(startRelativeAt.y * shadowSubdiv), shadowOffsetsCount)) ],
					shadowOffsets[ int(mod(floor(startRelativeAt.z * shadowSubdiv), shadowOffsetsCount)) ]
				);
				const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness * int(isSurfaceBlock(i.surface));
				
				const vec4 q = vec4(normalize(vec3(1+sin(time/4)/10,3,2)), cos(time/4)/5);
				const vec3 v = normalize(vec3(-1, 4, 2) + offset);
				const vec3 temp = cross(q.xyz, v) + q.w * v;
				const vec3 rotated = v + 2.0*cross(q.xyz, temp);
				
				const vec3 dir = normalize(rotated);
				
				const vec3 position = isSurfaceBlock(i.surface) ? ( floor(startRelativeAt * shadowSubdiv) + (1-abs(i.normal))*0.5 )/shadowSubdiv : i.at;
				ray = Ray( position + normalize(i.normal) * 0.0001, dir );
				chunkIndex = intersectionChunkIndex;
				
				shadowIndex = curSteps++;
				steps[shadowIndex] = Step( col * mix(0.45, 1.0, pow(light, 2)), t, surfaceBlock(blockId) );
				continue;
			}
		}
		else {
			steps[curSteps] = Step( background(ray.dir), far, noSurface() );
				
			break;
		}
	}
	
	const Step last = steps[curSteps--];
	
	Step previous = last;
	
	if(shadow) {
		if(isNoSurface(last.surface)) {
			previous = Step(vec3(1), 1, noSurface());
			for(; curSteps > shadowIndex; curSteps--) {
				const Step current = steps[curSteps];
				previous = combineSteps(current, previous);
			}
			//curSteps == shadowIndex
			const Step current = steps[curSteps--]; 
			
			previous = Step( current.color * previous.color, current.depth, current.surface );
		}
		else {
			curSteps = shadowIndex;
			const Step current = steps[curSteps--]; 
			previous = Step( current.color * 0.4, current.depth, current.surface );
		}
	}
	//else if(!isNoSurface(last.surface)) previous = Step( vec3(0), 1, noSurface() );
	
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

	const Trace trc = trace(ray, startChunkIndex);
	
	const vec3 col = trc.color;
	const float t = clamp(trc.depth, near, far);
	
	if(length(gl_FragCoord.xy - windowSize / 2) < 3) color = vec4(vec3(0.98), 1);
	else 
		color = vec4(col, 1);
}	