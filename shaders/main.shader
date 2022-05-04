#version 450

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
const int blocksInChunkDimAsPow2 = 4;
const int blocksInChunkDim = 1 << blocksInChunkDimAsPow2;
const int blocksInChunkCount = blocksInChunkDim*blocksInChunkDim*blocksInChunkDim;

const int cubesInBlockDimAsPow2 = 1;
const int cubesInBlockDim = 1 << cubesInBlockDimAsPow2;
const int cubesInBlockCount = cubesInBlockDim*cubesInBlockDim*cubesInBlockDim;
 
const int cubesInChunkDimAsPow2 = cubesInBlockDimAsPow2 + blocksInChunkDimAsPow2;
const int cubesInChunkDim = 1 << cubesInChunkDimAsPow2;
const int cubesInChunkCount = cubesInChunkDim*cubesInChunkDim*cubesInChunkDim;



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
	return ivec3( data % blocksInChunkDim, (data / blocksInChunkDim) % blocksInChunkDim, (data / blocksInChunkDim / blocksInChunkDim) );
}
uint blockIndex(const ivec3 coord) {
	return uint(coord.x + coord.y*blocksInChunkDim + coord.z*blocksInChunkDim*blocksInChunkDim);
}
ivec3 start(const uint data) { return indexBlock(data&65535u); } //copied from Chunks.h
ivec3 end(const uint data) { return indexBlock((data>>16)&65535u); } //copied from Chunks.h
ivec3 onePastEnd(const uint data) { return end(data) + 1; } //copied from Chunks.h
bool emptyBounds(const ivec3 start, const ivec3 onePastEnd) {
	return any(lessThanEqual(onePastEnd, start));
}
bool emptyBoundsF(const vec3 start, const vec3 onePastEnd) {
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
	return vec3( (chunkPosition(chunkIndex) - playerChunk) * blocksInChunkDim ) - playerInChunk;
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
	#define test(i) ( int(i >= 0) - int(i < blocksInChunkDim) )
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
  bool hasNoNeighbours(const Block block) {
	  return ((block.data >> 17) & 1) != 0;
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
		blockCoord = blockCoord - outDir * blocksInChunkDim;
	}
	
	return blockAt(chunkIndex, blockCoord);
}

layout(binding = 6) restrict readonly buffer ChunksAO {
    uint data[];
} ao;

//copied from Chunks.h chunk::cubeIndexInChunk 
  int cubeIndexInChunk(const ivec3 coord) {
  	return coord.x + coord.y*cubesInChunkDim + coord.z*cubesInChunkDim*cubesInChunkDim;
  }
  
uint aoAt(const int chunkIndex, const ivec3 vertexCoord) {
	const int startIndex = chunkIndex * cubesInChunkCount;
	const int offset = cubeIndexInChunk(vertexCoord);
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
		cubeCoord -= outDir * cubesInChunkDim;
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


layout(binding = 7) restrict readonly buffer ChunksSkyLighting {
    uint data[];
} skyLighting;

layout(binding = 8) restrict readonly buffer ChunksBlockLighting {
    uint data[];
} blockLighting;

float lightingAtCube(int chunkIndex, ivec3 cubeCoord) {
	const ivec3 outDir = testBounds(shr3i(cubeCoord, 1));
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkNeighbourIndex(chunkIndex, outDir);
		if(candChunkIndex < 0) return 1;
		
		chunkIndex = candChunkIndex;
		cubeCoord -= outDir * cubesInChunkDim;
	}
	
	const int startIndex = chunkIndex * cubesInChunkCount;
	const int offset = cubeIndexInChunk(cubeCoord);
	const int index = startIndex + offset;
	
	const int el = index / 4;
	const int sh = (index % 4) * 8;
	
	const uint lightingInt = max(
		((skyLighting.data[el] >> sh) & 255), 
		((blockLighting.data[el] >> sh) & 255)
	);
	
	const float light = float(lightingInt) / 31.0;
	return 0.02 + pow(light, 2.2) * 0.98;
}

//chunk::Chunk3x3BlocksList
layout(binding = 9) restrict readonly buffer ChunksNeighbourngEmitters {
    uint data[];
} emitters;
  const int neSidelength = 3 * blocksInChunkDim;
  const int neCapacity = 30;
  
  int neCoordToIndex(const ivec3 coord) {
  	return (coord.x + blocksInChunkDim)
  		 + (coord.y + blocksInChunkDim) * neSidelength
  		 + (coord.z + blocksInChunkDim) * neSidelength * neSidelength;
  }
  ivec3 neIndexToCoord(const int index) {
  	return ivec3(
  		index % neSidelength,
  		(index / neSidelength) % neSidelength,
  		index / neSidelength / neSidelength
  	) - blocksInChunkDim;
  }

  struct NE { ivec3 coord; bool is; };
  NE neFromChunk(const int chunkIndex, const int neIndex) { 
	const int chunkOffset = chunkIndex * 16;
	const int neOffset = neIndex / 2;
	const int neShift = (neIndex % 2) * 16;
	
	const uint first = emitters.data[chunkOffset];
	const uint ne16 = (emitters.data[chunkOffset + 1 + neOffset] >> neShift) & 0xffff;
	
	const uint ne_index = ne16 | (((first >> (2 + neIndex))&1)<<16);
	const ivec3 ne_coord = neIndexToCoord(int(ne_index));
	
	return NE(ne_coord, bool(first & 1));
  }

/*uint emittersDataInChunk(const int chunkIndex, const int dataIndex) {
	const int index_ = chunkIndex * 16 + dataIndex;
	const int index = index_ / 2;
	const int shift = (index_ % 2) * 16;
	return (emitters.data[index] >> shift) & 0xffff;
}

uint emittersCountInChunk(const int chunkIndex) {
	return emittersDataInChunk(chunkIndex, 0);
}

uint emitterIndexInChunk(const int chunkIndex, const int emitterIndex) {
	return emittersDataInChunk(chunkIndex, emitterIndex+1);
}*/

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

float intersectSquare(const Ray r, const vec3 center, const vec3 n, const vec3 up, const vec3 left) {
    const vec3 c = center + n; //plane center
    const float t = intersectPlane(r, c, n);
    const vec3 p = at(r, t); //point
    const vec3 l = p - c; //point local to plane
    float u_ = dot(l, up);
    float v_ = dot(l, left);
    if (abs(u_) <= 1 && abs(v_) <= 1) {
        //const vec2 uv = (vec2(u_, v_) / 2 + 0.5);
        return t;
    }
    return 1.0 / 0.0;
}

struct Intersection {
	vec3 normal;
	vec3 at;
	bool is;
	bool frontface;
};

Intersection noIntersection() {
	 Intersection i;
	 i.is = false;
	 return i;
}

Intersection intersectCube(const Ray ray, const vec3 start, const vec3 end, const vec3 n1, const vec3 n2) {
	const vec3 size = (end - start) / 2 * 0.999;
	const vec3 center = (end + start) / 2; 
	const float frontface = float(all(equal(ray.orig, clamp(ray.orig, start, end)))) * -2 + 1;
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
	
    const uint sides = 3;
    const float arr[sides] = {
         intersectSquare(ray, center, nn[0] * size.x, ns[2], ns[1])
        ,intersectSquare(ray, center, nn[1] * size.y, ns[0], ns[2])
        ,intersectSquare(ray, center, nn[2] * size.z, ns[0], ns[1])
    };
	
    float shortestT = 1.0 / 0.0;
	vec3 normal;
	
	for (uint i = 0; i < sides; i++) {
		float t = arr[i];
		if (t > 0 && t < shortestT) {
			shortestT = t;
			normal = nn[i];
		}
	}
	
	return Intersection(
		normal * frontface,
		mix(center + normal * size, at(ray, shortestT), equal(normal, vec3(0))),
		shortestT < 1.0 / 0.0,
		frontface > 0
	);
}

struct IntersectionInfo {
	vec3 coord;
	vec3 localSpaceCoord;
	int chunkIndex;
	int bias;
	uint fromBlockId_toBlockId;
};

IntersectionInfo noIntersectionInfo() {
	IntersectionInfo i;
	i.fromBlockId_toBlockId = 0;
	i.chunkIndex = -1;
	return i;
}

IntersectionInfo isInters(const Ray ray, ivec3 relativeToChunk_, const int /*must be loaded*/ startChunkIndex, const int startBias, const float stopAtLen /*and return no intersection with chunkIndex*/) { 
	const vec3 dir = ray.dir;
	const vec3 dirSign = sign(dir);
	const vec3 positive_ = max(+dirSign, vec3(0));
	const vec3 negative_ = max(-dirSign, vec3(0));

	const vec3 stepLength = 1 / abs(dir);
	
	Block fromBlock;
	int curChunkIndex = startChunkIndex;
	int bias = startBias;
	vec3 curCoord = ray.orig;
	vec3 relativeTo = relativeToChunk_ * blocksInChunkDim;
	
	{ //calculate first intersection
		const ivec3 startBlockRalativeCoord = ivec3(floor(curCoord) - positive_ * vec3(equal(curCoord, floor(curCoord))) - relativeTo);
		const ivec3 startBlockNeighbourChunkDir = testBounds(startBlockRalativeCoord);
		const int startBlockNeighbourChunk = chunkNeighbourIndex(curChunkIndex, startBlockNeighbourChunkDir);
		fromBlock = startBlockNeighbourChunk == -1 ? blockAir() : blockAt( //not loaded chunks are unaccounted
			startBlockNeighbourChunk,
			startBlockRalativeCoord - startBlockNeighbourChunkDir * blocksInChunkDim
		);
		
		const float skipDistance = blockId(fromBlock) == 0 ? 1.0 : cubesInBlockDim;
		const vec3 candCoords = ceil(curCoord*skipDistance * dirSign)/skipDistance*dirSign;		
		
		const vec3 nextLenghts = (candCoords - ray.orig) * dirSign * stepLength;
		const float nextMinLen = min(min(nextLenghts.x, nextLenghts.y), nextLenghts.z);
		const bvec3 nextMinAxisB = equal(nextLenghts, vec3(nextMinLen));
				
		if(!any(equal(curCoord, candCoords) && nextMinAxisB)) bias = -1;
		
		curCoord = mix(
			at(ray, nextMinLen), 
			candCoords, 
			nextMinAxisB
		);
		
	}
	
	int steps = 0;
	while(true) {
		if(curChunkIndex == -1) break;
		
		//calculate next chunk
		const vec3 relativeOrig = ray.orig - relativeTo;
		const vec3 maxOutBorderDiff = mix(relativeOrig, blocksInChunkDim - relativeOrig, positive_);
		const vec3 minOutLen_vec = maxOutBorderDiff*stepLength;
		
		const float minOutLen = min(min(minOutLen_vec.x, minOutLen_vec.y), minOutLen_vec.z); //minimum length for any ray axis to get outside of chunk bounds
		const bvec3 firstOut = equal(minOutLen_vec, vec3(minOutLen));
	
		const ivec3 outNeighbourDir = ivec3(firstOut) * ivec3(dirSign);
		const int candChunkIndex = chunkNeighbourIndex(curChunkIndex, outNeighbourDir);
		//
		const int nextChunkIndex = candChunkIndex;
		const vec3 nextRelativeTo = relativeTo + outNeighbourDir * blocksInChunkDim;
		const bool nextNotLoaded = chunkNotLoaded(nextChunkIndex);
		
		//calculate first intersection with current chunk
		const uint bounds = chunkBounds(curChunkIndex);
		const vec3 startBorder = vec3(start(bounds));
		const vec3 endBorder   = vec3(onePastEnd(bounds));
		const bool empty = emptyBoundsF(startBorder, endBorder);
		
		const vec3 nearBoundaries = mix(startBorder, endBorder, negative_);
		const vec3 farBoundaries  = mix(startBorder, endBorder, positive_);
		
		const bool pastNearBounds = all(greaterThanEqual((curCoord - relativeTo - nearBoundaries)*dirSign, vec3(0)));
		
		const vec3  nearBoundsCandCoords = nearBoundaries + relativeTo;
		const vec3  nearBoundsCandLenghts = (nearBoundsCandCoords - ray.orig) * dirSign * stepLength;
		const float nearBoundsCandMinLen = max(max(nearBoundsCandLenghts.x, nearBoundsCandLenghts.y), nearBoundsCandLenghts.z);  //minimum length for all ray axis to get inside of chunk bounds
		const bvec3 nearBoundCandMinB = equal(nearBoundsCandLenghts, vec3(nearBoundsCandMinLen));
		
		const vec3 candCoord = pastNearBounds ? curCoord : mix(at(ray, nearBoundsCandMinLen), nearBoundsCandCoords, nearBoundCandMinB);
		
		const bvec3 inChunkBounds = lessThanEqual((candCoord - relativeTo - farBoundaries) * dirSign, vec3(0,0,0));
		if(!all(inChunkBounds) || empty) {
			curChunkIndex = nextNotLoaded ? -1 : nextChunkIndex;
			relativeTo = nextRelativeTo;
			continue;
		}
		
		const bool stopInThisChunk = stopAtLen <= minOutLen;
		
		const vec3 candFromBlockCoord = floor(candCoord) - positive_ * vec3(equal(candCoord, floor(candCoord)));
		const vec3 curFromBlockCoord  = floor(curCoord ) - positive_ * vec3(equal(curCoord , floor(curCoord) ));
		
		if(candFromBlockCoord != curFromBlockCoord) fromBlock = blockAir();
		curCoord = candCoord;
		if(!pastNearBounds) bias = -1;
		
		//step through blocks/cubes of the current chunk
		while(true) {
			const bvec3 blockBounds = equal(curCoord, floor(curCoord));
			const bool atBlockBounds = any(blockBounds);
					
			const vec3 fromBlockCoord = floor(curCoord - positive_ * vec3(blockBounds));
			const vec3 toBlockCoord   = floor(curCoord - negative_ * vec3(blockBounds));
			
			Block toBlock;
			//if(!atBlockBounds) toBlock = fromBlock;
			//else {
				const ivec3 relativeToBlockCoordI = ivec3(toBlockCoord - relativeTo);
				if(checkBoundaries(relativeToBlockCoordI)) toBlock = blockAt(
					curChunkIndex,
					relativeToBlockCoordI
				);
				else if(nextNotLoaded) toBlock = blockAir();
				else toBlock = blockAt(
					nextChunkIndex,
					ivec3(toBlockCoord - nextRelativeTo) //and3i(relativeToBlockCoordI + blocksInChunkDim, blocksInChunkDim-1)
				);
			//}
			
			const Block biasedFromBlock = bias < 0 ? fromBlock : blockAir();
			const Block biasedToBlock   = bias < 1 ? toBlock   : blockAir();
			
			const uint fromBlockCubes = blockCubes(biasedFromBlock);
			const uint    fromBlockId = blockId   (biasedFromBlock);
			const uint   toBlockCubes = blockCubes(  biasedToBlock);
			const uint      toBlockId = blockId   (  biasedToBlock);
			
			if(fromBlockId != 0 || toBlockId != 0) {
				const bvec3 cubeBounds = equal(curCoord*cubesInBlockDim, floor(curCoord*cubesInBlockDim));
				const vec3 localCubesCoordAt = curCoord - fromBlockCoord;
				
				const ivec3 fUpperCubes = ivec3(mod(localCubesCoordAt*cubesInBlockDim - positive_ * vec3(cubeBounds), cubesInBlockDim));
				const ivec3 tUpperCubes = ivec3(mod(localCubesCoordAt*cubesInBlockDim - negative_ * vec3(cubeBounds), cubesInBlockDim));
				
				const bvec2 cubes = bvec2(
					cubeAt(fromBlockCubes, fUpperCubes),
					cubeAt(toBlockCubes  , tUpperCubes)
				);
				
				const uint fBlockUsedId = cubes.x ? fromBlockId : 0;
				const uint tBlockUsedId = cubes.y ? toBlockId   : 0;
				
				
				if(!(all(cubes) || all(not(cubes))) || (atBlockBounds && !(fBlockUsedId == tBlockUsedId && fBlockUsedId == 7))) {
					const ivec3 intersectionSide = ivec3(cubeBounds);
					
					const uint blocks[] = { fBlockUsedId, tBlockUsedId };
					const vec3 blocksCoord[] = { fromBlockCoord, toBlockCoord };
					
					for(bias = max(bias, -1 + int(blocks[0]==0)); bias < 1 - int(blocks[1]==0); bias ++) {
						const uint blockId = blocks[bias+1];
						const vec3 blockCoord = blocksCoord[bias+1];
						
						if(blockId != 0) { 
							const vec3 localBlockCoord = curCoord - blockCoord;
							const vec2 uv = vec2(
								dot(intersectionSide, localBlockCoord.zxx),
								dot(intersectionSide, localBlockCoord.yzy)
							);
			
							if(blockId == 5) { //leaves
								if(int(dot(ivec2(uv * vec2(4, 8)), vec2(1))) % 2 != 0) continue;
							}
						
							if(stopAtLen <= length(curCoord - ray.orig)) return IntersectionInfo(
								vec3(0),
								vec3(0),
								curChunkIndex,
								0,
								0u
							);
							
							return IntersectionInfo(
								curCoord,
								localBlockCoord,
								curChunkIndex,
								bias,
								(fBlockUsedId & 0xffff) | ((tBlockUsedId & 0xffff) << 16)
							);
						}
					}
				}		
			}
			
			
			fromBlock = toBlock;
			const bool hnn = hasNoNeighbours(fromBlock);
			const float skipDistance = hnn ? 1 : (blockId(fromBlock) == 0 ? 1.0 : cubesInBlockDim);
			const vec3 candCoords = floor(curCoord*skipDistance * dirSign + (hnn ? 2 : 1))/skipDistance*dirSign;

			const vec3 nextLenghts = (candCoords - ray.orig) * dirSign * stepLength;
			const float nextMinLen = min(min(nextLenghts.x, nextLenghts.y), nextLenghts.z);
			const bvec3 nextMinAxisB = equal(nextLenghts, vec3(nextMinLen));
			
			curCoord = mix(
				max(curCoord*dirSign, at(ray, nextMinLen.x)*dirSign)*dirSign, 
				candCoords, 
				nextMinAxisB
			);
			//if(hnn) discard;//steps++;
			bias = -1;
			
			const bool inChunkBounds = all(lessThan((curCoord - relativeTo - farBoundaries) * dirSign, vec3(0,0,0)) );
	
			if(!inChunkBounds) {
				if(stopInThisChunk) break;
				curChunkIndex = nextNotLoaded ? -1 : nextChunkIndex;
				relativeTo = nextRelativeTo;
				break;
			}
		}
		
		if(stopInThisChunk) return IntersectionInfo(
			vec3(0),
			vec3(0),
			curChunkIndex,
			0,
			0u
		);
	}
	return noIntersectionInfo();
}



vec3 background(const vec3 dir) {
	const float t = 0.5 * (dir.y + 1.0);
	const vec3 res = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
	return pow(res, vec3(2.2));
}

struct Frame {
	uint data[7];
	int parent;
};

const int maxFrames = 5;
Frame frames[maxFrames];

void setFrame(const int frameIndex, const Frame frame) {
	frames[frameIndex] = frame;
}
void putFrame(const int frameIndex, const Frame frame) {
	setFrame(frameIndex, frame);
}

Frame getFrame(const int frameIndex) {
	return frames[frameIndex];
}

struct Params {
	Ray ray;
	int chunkIndex;
	int bias;
	uint type;
}; 

struct Result {
	vec3 color;
	float depth;
	Surface surface;
	uint type;
};

Frame packParams(const Params it, const int parent) {
	return Frame(
		uint[7] (
			floatBitsToUint(it.ray.orig.x),
			floatBitsToUint(it.ray.orig.y),
			floatBitsToUint(it.ray.orig.z),
			
			floatBitsToUint(it.ray.dir.x),
			floatBitsToUint(it.ray.dir.y),
			floatBitsToUint(it.ray.dir.z),
			
			uint(it.chunkIndex & 0xffffff) | (uint(sign(it.bias)+1) << 24) | (it.type << 26)
		),	
		parent
	);
}
Params unpackParams(const Frame it) {
	return Params(
		Ray(
			vec3(
				uintBitsToFloat(it.data[0]),
				uintBitsToFloat(it.data[1]),
				uintBitsToFloat(it.data[2])
			),
			vec3(
				uintBitsToFloat(it.data[3]),
				uintBitsToFloat(it.data[4]),
				uintBitsToFloat(it.data[5])
			)
		),
		int(it.data[6] & 0xffffff),
		int((it.data[6] >> 24) & 3)-1,
		uint(it.data[6] >> 26)
	);
}


Result unpackResult(const Frame it) {
	return Result(
		vec3(
			uintBitsToFloat(it.data[0]),
			uintBitsToFloat(it.data[1]),
			uintBitsToFloat(it.data[2])
		),
		uintBitsToFloat(it.data[3]),
		Surface(it.data[4]),
		it.data[5]
	);
}
Frame packResult(const Result it, const int parent) {
	return Frame(
		uint[7] (
			floatBitsToUint(it.color.x),
			floatBitsToUint(it.color.y),
			floatBitsToUint(it.color.z),
			
			floatBitsToUint(it.depth),
			it.surface.data,
			it.type,
			0u
		),
		parent
	);
}

int mapInteger(int value) {//https://stackoverflow.com/a/24771093/18704284
    value *= 1664525;
    value += 101390223;
    value ^= value >> 11;
    value ^= value << 16;
    value ^= value >> 23;
    value *= 110351245;
    value += 12345;

    return value;
}

Result combineSteps(const Result current, const Result inner) {
	const bool curShadow = current.type == 1;
	const bool innerShadow = inner.type == 1 || inner.type == 2;
	
	const Surface resultingSurface = innerShadow ? inner.surface : current.surface;
	
	if(innerShadow && !curShadow) {
		const bool lighting = inner.type == 2;
		const uint innerSurface = surfaceType(inner.surface);
		const bool emitter = innerSurface == 13 || innerSurface == 14;
		const vec3 shadowTint = lighting ? vec3(1) : (surfaceType(resultingSurface) == 0 ? inner.color : vec3(0.4));
		return Result(current.color * shadowTint + (lighting && emitter ? (inner.color / (0.01 + inner.depth*inner.depth)) : vec3(0)), current.depth + inner.depth, current.surface, current.type);
	}
	
	const uint type = surfaceType(current.surface);
	
	if(type == 7) return Result(current.color * inner.color, current.depth + inner.depth, resultingSurface, current.type);
	else if(type == 8) return Result(inner.color * current.color, current.depth + inner.depth, resultingSurface, current.type);
	else return Result(current.color, current.depth, resultingSurface, current.type);
}

vec3 trace(const Ray startRay, const int startChunkIndex) {
	int curFrame = -1;
	int newFrame = curFrame+1;

	const ivec3 startChunkPosition = chunkPosition(startChunkIndex);
	
	putFrame(newFrame++, packParams(Params(startRay, startChunkIndex, 0, 0u), curFrame));
	
	while(true) {
		curFrame++;
		if(curFrame >= newFrame) { curFrame = newFrame-1; break; }
		
		const Frame curentFrame = getFrame(curFrame);
		const Params p = unpackParams(curentFrame);
		const Ray ray = p.ray;
		const int chunkIndex = p.chunkIndex;
		const uint type = p.type;
		const int startBias = p.bias;
		const int parent = curentFrame.parent;
		
		const bool shadow = type == 1 || type == 2;
		
		const vec3 startP = playerRelativePosition - (vec3(0.3, 1.95/2, 0.3));
		const vec3 endP   = playerRelativePosition + (vec3(0.3, 1.95/2, 0.3));
		
		const Intersection playerIntersection = intersectCube(ray, startP, endP, vec3(1,0,0), vec3(0,1,0));
		const bool isPlayer = (drawPlayer || curFrame != 0) && playerIntersection.is;
		const float playerT = isPlayer ? length(playerIntersection.at - ray.orig) : 1.0 / 0.0;
		
		const IntersectionInfo i = isInters(ray, chunkPosition(chunkIndex) - startChunkPosition, chunkIndex, startBias, playerT);
		
		if(i.fromBlockId_toBlockId != 0) { //block
			const vec3 coord = i.coord;        
			const vec3 localSpaceCoord = i.localSpaceCoord;
			const int intersectionChunkIndex = i.chunkIndex;
			const int bias = i.bias;
			const uint fromBlockId = i.fromBlockId_toBlockId & 0xffff;
			const uint toBlockId = (i.fromBlockId_toBlockId >> 16) & 0xffff;
			const uint blockId = bias < 0 ? fromBlockId : toBlockId;
			
			const bvec3 intersectionSideB = equal(localSpaceCoord * cubesInBlockDim, floor(localSpaceCoord * cubesInBlockDim));
			const ivec3 intersectionSide = ivec3(intersectionSideB);
			
			const ivec3 side = intersectionSide * (-bias*2 - 1) * ivec3(sign(ray.dir));
			const bool backside = dot(vec3(side), ray.dir) > 0;
			const ivec3 normal = side * ( backside ? -1 : 1 );
			
			const vec2 uv = clamp(vec2(
				dot(intersectionSide, localSpaceCoord.zxx),
				dot(intersectionSide, localSpaceCoord.yzy)
			), 0.0001, 0.9999);
			
			const vec2 atlasOffset = atlasAt(blockId, side);
			
			const float t = length(coord - ray.orig);
			
			float light = 1;
			float ambient = 1;
			
			if(!shadow) {
				const ivec3 relativeToChunk = chunkPosition(intersectionChunkIndex) - startChunkPosition;
				
				const ivec3 otherAxis1 = intersectionSideB.x ? ivec3(0,1,0) : ivec3(1,0,0);
				const ivec3 otherAxis2 = intersectionSideB.z ? ivec3(0,1,0) : ivec3(0,0,1);
				const ivec3 vertexCoord = ivec3(floor(coord * cubesInBlockDim)) - relativeToChunk * cubesInChunkDim;
				
				ambient = 0;
				if(!backside) {
					for(int i = 0 ; i < 4; i++) {
						const ivec2 offset_ = ivec2(i%2, i/2);
						const ivec3 offset = offset_.x * otherAxis1 + offset_.y * otherAxis2;
						const ivec3 curVertexCoord = vertexCoord + offset;
											
						const float vertexLight = lightForChunkVertexDir(intersectionChunkIndex, curVertexCoord, normal);
						
						const float diffX = abs(offset_.x - mod(dot(localSpaceCoord, otherAxis1)*2, 1));
						const float diffY = abs(offset_.y - mod(dot(localSpaceCoord, otherAxis2)*2, 1));
						ambient += mix(mix(vertexLight, 0, diffY), 0, diffX);
					}
				}
				else ambient = 0.5;
				
				const ivec3 dirSignI = ivec3(sign(ray.dir));
				const ivec3 positive_ = max(dirSignI, ivec3(0));
				const ivec3 negative_ = max(-dirSignI, ivec3(0));
				
				light = max(
					float(bias >= 0) * lightingAtCube(intersectionChunkIndex, vertexCoord - negative_ * intersectionSide),
					float(bias <= 0) * lightingAtCube(intersectionChunkIndex, vertexCoord - positive_ * intersectionSide)
				);
			}
			
			
			if(blockId == 7) {	
				const vec3 glassOffset = vec3( 
					sin(localSpaceCoord.x*9)/2, 
					sin(localSpaceCoord.z*9)/8, 
					sin(localSpaceCoord.y*6 + localSpaceCoord.x*4)/8
				) / 100;
				
				const vec3 glassBlockCoord = localSpaceCoord - glassOffset;
				
				vec2 glassUv = vec2(
					dot(intersectionSide, glassBlockCoord.zxx),
					dot(intersectionSide, glassBlockCoord.yzy)
				);
				const vec2 offset = atlasAt(blockId, side);
				const vec3 color = sampleAtlas(atlasOffset, clamp(glassUv, 0.0001, 0.9999)) * light * mix(0.6, 1.0, ambient);
			
				const vec3 incoming = ray.dir - glassOffset * vec3(1-intersectionSide);
				
				const vec3 refracted_ = refract(normalize(incoming), normalize(normal), 1 );
				const bool isRefracted = refracted_ != 0;
				const vec3 newDir = isRefracted ? refracted_ : reflect(incoming, normalize(vec3(side)));
				const int newBias = sign( isRefracted ? bias + 1 : -bias );
				
				putFrame(curFrame, packResult(Result( color, t, surfaceBlock(blockId), type ), parent));
				if(newFrame < maxFrames) 
					putFrame(newFrame++, packParams(Params( Ray(coord, newDir), intersectionChunkIndex, newBias, type ), curFrame));
				continue;
			}
			else if(blockId == 8) {
				const vec3 color = sampleAtlas(atlasOffset, uv) * light * mix(0.5, 1.0, ambient);
				const vec3 offset = normalize(texture2D(noise, uv/5).xyz - 0.5) * 0.01;
				const vec3 reflected_ = reflect( ray.dir, normalize( normal + offset ) );
				const vec3 reflected  = abs(reflected_) * normal + reflected_ * (1 - abs(normal));
				
				putFrame(curFrame, packResult(Result( color, t, surfaceBlock(blockId), type ), parent));
				if(newFrame < maxFrames) 
					putFrame(newFrame++, packParams(Params( Ray(coord, reflected), intersectionChunkIndex, -bias, type ), curFrame));
				continue;
			}			
			else {
				const bool emitter = blockId == 13 || blockId == 14;
				const ivec3 relativeToChunk = chunkPosition(intersectionChunkIndex) - startChunkPosition;
				const vec3 color = sampleAtlas(atlasOffset, uv) * (emitter ? 1.0 : light * mix(0.4, 1.0, ambient));
				
				putFrame(curFrame, packResult(Result( color, t, surfaceBlock(blockId), type ), parent));
				
				if(shadow) continue;
				else {
					const int shadowOffsetsCount = 4;
					const float shadowOffsets[] = { -1, 0, 1, 0 };
					
					const int shadowSubdiv = 32;
					const float shadowSmoothness = 32;
					const int shadowsInChunkDim = blocksInChunkDim * shadowSubdiv;		
					
					if(emitter) continue;
					
					const vec3 position = ( floor(coord * shadowSubdiv) + (1-abs(normal))*0.5 )/shadowSubdiv;
					
					const ivec3 shadowInChunkCoord = ivec3(floor(mod(coord * shadowSubdiv, vec3(shadowsInChunkDim))));
					const int shadowInChunkIndex = 
						  shadowInChunkCoord.x
						+ shadowInChunkCoord.y * shadowsInChunkDim
						+ shadowInChunkCoord.z * shadowsInChunkDim * shadowsInChunkDim; //should fit in 31 bit
					const int rShadowInChunkIndex = mapInteger(shadowInChunkIndex);
					const int dirIndex = int(uint(rShadowInChunkIndex) % 42);
					const bool emitters = dirIndex < 30;
					
					if(newFrame < maxFrames) { //shadow
						const vec3 offset_ = vec3(
							shadowOffsets[ int(mod(floor(coord.x * shadowSubdiv), shadowOffsetsCount)) ],
							shadowOffsets[ int(mod(floor(coord.y * shadowSubdiv), shadowOffsetsCount)) ],
							shadowOffsets[ int(mod(floor(coord.z * shadowSubdiv), shadowOffsetsCount)) ]
						);
						const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness;
						const vec4 q = vec4(normalize(vec3(1+sin(time/4)/10,3,2)), cos(time/4)/5);
						const vec3 v = normalize(vec3(-1, 4, 2) + offset);
						const vec3 temp = cross(q.xyz, v) + q.w * v;
						const vec3 rotated = v + 2.0*cross(q.xyz, temp);
						
						const vec3 newDir = normalize(rotated);
						const int newBias = bias * int(sign(dot(ray.dir*newDir, intersectionSide)));
						
						putFrame(newFrame++, packParams(Params( Ray(position, newDir), intersectionChunkIndex, newBias, 1u), curFrame));
					}
					if(emitters && newFrame < maxFrames) {
						const NE ne = neFromChunk(intersectionChunkIndex, dirIndex);
						
						if(ne.is) {
							const vec3 relativeCoord = position - relativeToChunk * blocksInChunkDim;
							const vec3 newDir = normalize(ne.coord+0.5 - relativeCoord);
							const int newBias = bias * int(sign(dot(ray.dir*newDir, intersectionSide)));
							
							putFrame(newFrame++, packParams(Params( Ray(position, newDir), intersectionChunkIndex, newBias, 2u), curFrame));
						}
					}
					continue;
				}
			}
		}
		else if(isPlayer && i.chunkIndex != -1) { //player
			putFrame(curFrame, packResult(Result( vec3(1), playerT, surfaceEntity(1), type ), parent));
			if(shadow) continue;
			
			const vec4 q = vec4(normalize(vec3(1+sin(time/4)/10,3,2)), cos(time/4)/5);
			const vec3 v = normalize(vec3(-1, 4, 2));
			const vec3 temp = cross(q.xyz, v) + q.w * v;
			const vec3 rotated = v + 2.0*cross(q.xyz, temp);
			
			const vec3 newDir = normalize(rotated);
			const Ray newRay = Ray( playerIntersection.at + playerIntersection.normal * 0.001, newDir );
			if(newFrame < maxFrames) 
				putFrame(newFrame++, packParams(Params( newRay, i.chunkIndex, 0, 1 ), curFrame));

			continue;
		}
		else { //sky
			putFrame(curFrame, packResult(Result( shadow ? vec3(1) : background(ray.dir), far, noSurface(), type ), parent));
			continue;
		}
	}
	
	for(;curFrame >= 0; curFrame--) {
		const Frame current = getFrame(curFrame);
		const int currentParent = current.parent;
		if(currentParent == -1) break;
		const Result currentResult = unpackResult(current);
		
		const Frame outer = getFrame(currentParent);
		const Result outerResult = unpackResult(outer);
		const int outertParent = outer.parent;
		
		//if(currentResult.type == 2) return vec3(1,0,1);
		putFrame(currentParent, packResult(combineSteps(outerResult, currentResult), outertParent));
	}
	
	return unpackResult(getFrame(0)).color;
}

void main() {
	const vec2 uv = gl_FragCoord.xy / windowSize.xy;
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const vec3 relativeChunkPos = relativeChunkPosition(startChunkIndex);
	const Ray ray = Ray(-relativeChunkPos, rayDir);

	const vec3 col = trace(ray, startChunkIndex);
	
	const float bw = 0.299 * col.r + 0.587 * col.g + 0.114 * col.b;
	const vec3 c = rgb2hsv(col);
	const vec3 c2 = hsv2rgb(vec3(c.x, 1 - exp(-2 / (bw+2)), c.z));
	
	if(length(gl_FragCoord.xy - windowSize / 2) < 3) color = vec4(vec3(0.98), 1);
	else color = vec4(col, 1);
}	