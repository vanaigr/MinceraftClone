#version 450

#extension GL_ARB_shader_group_vote : enable

in vec4 gl_FragCoord;
layout(location = 0) out vec4 color;

uniform uvec2 windowSize;
uniform vec3 rightDir, topDir;

uniform float time;

uniform sampler2D atlas;
uniform float atlasTileSize;

uniform mat4 projection; //from local space to screen

uniform float near;
uniform float far;

uniform sampler2D noise;

uniform vec3 playerRelativePosition;
uniform bool drawPlayer;
uniform float playerWidth;
uniform float playerHeight;

//these variables are used for debugging purpposes. Like `discard` but allows outputing different colors
//bool exit_ = false; 
//vec3 exitVec3 = vec3(5,5,5);

//copied from Units.h
const int blocksInChunkDimAsPow2 = 4;
const int blocksInChunkDim = 1 << blocksInChunkDimAsPow2;
const int blocksInChunkCount = blocksInChunkDim*blocksInChunkDim*blocksInChunkDim;

const int cubesInBlockDimAsPow2 = 1;
const int cubesInBlockDim = 1 << cubesInBlockDimAsPow2;
const int cubesInBlockCount = cubesInBlockDim*cubesInBlockDim*cubesInBlockDim;
 
const int cubesInChunkDimAsPow2 = cubesInBlockDimAsPow2 + blocksInChunkDimAsPow2;
const int cubesInChunkDim = 1 << cubesInChunkDimAsPow2;
const int cubesInChunkCount = cubesInChunkDim*cubesInChunkDim*cubesInChunkDim;

uniform vec3 startCoord;

ivec3 shr3i(const ivec3 v, const int i) {
	return ivec3(v.x >> i, v.y >> i, v.z >> i);
}
ivec3 and3i(const ivec3 v, const int i) {
	return ivec3(v.x & i, v.y & i, v.z & i);
}

ivec3 blockChunk(const ivec3 blockCoord) {
	return shr3i(blockCoord, blocksInChunkDimAsPow2);
}

ivec3 blockLocalToChunk(const ivec3 blockCoord) {
	return and3i(blockCoord, blocksInChunkDim-1);
}

ivec3 cubeBlock(const ivec3 cubeCoord) {
	return shr3i(cubeCoord, cubesInBlockDimAsPow2);
}

ivec3 cubeLocalToBlock(const ivec3 cubeCoord) {
	return and3i(cubeCoord, cubesInBlockDim-1);
}

ivec3 cubeChunk(const ivec3 cubeCoord) {
	return shr3i(cubeCoord, cubesInChunkDimAsPow2);
}

ivec3 cubeLocalToChunk(const ivec3 cubeCoord) {
	return and3i(cubeCoord, cubesInChunkDim-1);
}

struct Ray {
    vec3 orig;
    vec3 dir;
};


restrict readonly buffer ChunksBounds {
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

uniform int viewDistance;
const int renderDiameter = viewDistance*2 + 1;

//const int neighboursCount = 27;
restrict readonly buffer ChunksIndices {
    int data[];
} chunksIndices;

ivec3 chunkIndexPosition(const int index) {
	return ivec3(
		 index                                    % renderDiameter,
		(index / renderDiameter)                  % renderDiameter,
		(index / renderDiameter / renderDiameter) % renderDiameter
		
	);
}
int chunkPositionIndex(const ivec3 coord) {
	return coord.x + coord.y * renderDiameter + coord.z * renderDiameter * renderDiameter;
}

int chunkAtIndex(const int arrIndex) {	
	const int index = chunksIndices.data[arrIndex];
	return index;
}

int chunkAt(const ivec3 coord) {
	if(clamp(coord, 0, renderDiameter-1) != coord) return 0;
	return chunkAtIndex(chunkPositionIndex(coord));
}

restrict readonly buffer AtlasDescription {
    uint positions[]; //16bit xSide, 16bit ySide; 16bit xTop, 16bit yTop; 16bit xBot, 16bit yBot; 16bit xAlpha, 16bit yAlpha
} ad;

vec3 sampleAtlas(const vec2 offset, const vec2 coord) { //used in Main.cpp - Block hitbox fragment, Current block fragment
	const ivec2 size = textureSize(atlas, 0);
	const vec2 textureCoord = vec2(coord.x + offset.x, offset.y + 1-coord.y);
    const vec2 uv_ = vec2(textureCoord * atlasTileSize / vec2(size));
	const vec2 uv = vec2(uv_.x, 1 - uv_.y);
    return pow(texture(atlas, uv).rgb, vec3(2.2));
}

vec2 alphaAt(const uint id) {
	const int index = (int(id) * 4 + 3);
	const uint pos = ad.positions[index];
	return vec2( pos & 0xffffu, (pos>>16) & 0xffffu );
}

vec2 atlasAt(const uint id, const ivec3 side) {
	const int offset = int(side.y == 1) + int(side.y == -1) * 2;
	const int index = (int(id) * 4 + offset);
	const uint pos = ad.positions[index];
	return vec2( pos & 0xffffu, (pos>>16) & 0xffffu );
}

ivec3 testBounds(const ivec3 i) {
	#define test(i) ( int(i >= 0) - int(i < blocksInChunkDim) )
		return ivec3(test(i.x), test(i.y), test(i.z));
	#undef ch16
}

bool checkBoundaries(const ivec3 i) {
	return all(equal( testBounds(i), ivec3(0) ));
}


restrict readonly buffer ChunksMesh {
	uint data[];
} chunksMesh;

//copied from Chunk.h chunk::BlockData
struct Cubes {
	uint data;
};

bool blockCubeAt(const Cubes it, const ivec3 cubeCoord) { 
	const int index = 0 + cubeCoord.x + cubeCoord.y * 2 + cubeCoord.z * 4;
	return bool( (it.data >> index) & 1 );
}
bool liquidCubeAt(const Cubes it, const ivec3 cubeCoord) { 
	const int index = 8 + cubeCoord.x + cubeCoord.y * 2 + cubeCoord.z * 4;
	return bool( (it.data >> index) & 1 );
}
bool hasNoNeighbours(const Cubes it) {
  return bool((it.data >> 16) & 1);
}
bool fullSameLiquid(const Cubes it) {
  return bool((it.data >> 17) & 1);
}
bool neighboursFullSameLiquid(const Cubes it) {
  return bool((it.data >> 18) & 1);
}
bool isEmpty(const Cubes it) {
	return (it.data & 0xffffu) == 0;
}
Cubes cubesEmpty() {
	return Cubes(0);
}

Cubes cubesAtBlock(const int chunkIndex, const ivec3 blockCoord) {
	const int blockIndex = blockCoord.x + blockCoord.y * blocksInChunkDim + blockCoord.z * blocksInChunkDim * blocksInChunkDim;
	const int index = chunkIndex * blocksInChunkCount + blockIndex;

	const uint data = chunksMesh.data[index];
	return Cubes(data);
}


restrict readonly buffer ChunksBlocks {
	uint data[];
} chunksBlocks;

uint blockIdAt(const int chunkIndex, const ivec3 blockCoord) {
	const int inChunkIndex = blockCoord.x + blockCoord.y * blocksInChunkDim + blockCoord.z * blocksInChunkDim * blocksInChunkDim;
	const int index = chunkIndex * blocksInChunkCount + inChunkIndex;
	
	const int arrayIndex = index / 2;
	const int arrayOffset = index % 2;
	return (chunksBlocks.data[arrayIndex] >> (arrayOffset * 16)) & 0xffffu;
}


restrict readonly buffer ChunksLiquid {
	uint data[];
} chunksLiquid;

struct LiquidCube {
	uint data;
};

uint id(const LiquidCube it) { return it.data & 0xffffu; }
uint level(const LiquidCube it) { return (it.data>>16) & 0xffu; }

LiquidCube liquidAtCube(const int chunkIndex, const ivec3 cubeCoord) {
	const int cubeIndex = cubeCoord.x + cubeCoord.y * cubesInChunkDim + cubeCoord.z * cubesInChunkDim * cubesInChunkDim;
	const int index = chunkIndex * cubesInChunkCount + cubeIndex;

	const uint data = chunksLiquid.data[index];
	
	return LiquidCube(data);
}

restrict readonly buffer ChunksAO {
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

float lightForChunkVertexDir(const int chunkIndex, const ivec3 chunkCoord, const ivec3 vertex, const ivec3 dir) {
	const bvec3 dirMask = notEqual(dir, ivec3(0));
	const int dirCount = int(dirMask.x) + int(dirMask.y) + int(dirMask.z);
	if(dirCount == 0) return 0;
	const float maxLight = dirCount == 1 ? 4 : (dirCount == 2 ? 6 : 7);
	
	int curChunkIndex = chunkIndex;
	ivec3 cubeCoord = vertex;
	
	const ivec3 outDir = testBounds(shr3i(cubeCoord, cubesInBlockDimAsPow2));
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkAt(chunkCoord + outDir);
		if(candChunkIndex == 0) return 1;
		
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


restrict readonly buffer ChunksLighting {
    uint data[];
} lighting;

float lightingAtCube(int chunkIndex, const ivec3 chunkCoord, ivec3 cubeCoord) {
	const ivec3 outDir = testBounds(shr3i(cubeCoord, 1));
	if( !all(equal(outDir, ivec3(0))) ) {
		const int candChunkIndex = chunkAt(chunkCoord + outDir);
		if(candChunkIndex == 0) return 1;
		
		chunkIndex = candChunkIndex;
		cubeCoord -= outDir * cubesInChunkDim;
	}
	const int offset = cubeIndexInChunk(cubeCoord);
	const int sh = (offset % 4) * 8;
	
	const int startIndexSky = chunkIndex * cubesInChunkCount * 2;
	const int indexSky = startIndexSky + offset;	
	
	const int startIndexBlk = chunkIndex * cubesInChunkCount * 2 + cubesInChunkCount;
	const int indexBlk = startIndexBlk + offset;
	
	const int elSky = indexSky / 4;
	const int elBlk = indexBlk / 4;
	
	const uint lightingInt = max(
		((lighting.data[elSky] >> sh) & 255), 
		((lighting.data[elBlk] >> sh) & 255)
	);
	
	const float light = float(lightingInt) / 255.0;
	return 0.02 + pow(light, 2.2) * 0.98;
}

//chunk::Chunk3x3BlocksList
restrict readonly buffer ChunksNeighbourngEmitters {
    uint data[];
} emitters;
  const int neSidelength = 3 * blocksInChunkDim;
  const int neCapacity = 30;
  
  uint neCoordToIndex(const ivec3 coord) {
  	return (coord.x + blocksInChunkDim)
  		 + (coord.y + blocksInChunkDim) * neSidelength
  		 + (coord.z + blocksInChunkDim) * neSidelength * neSidelength;
  }
  ivec3 neIndexToCoord(const uint index) {
  	return ivec3(
  		index % neSidelength,
  		(index / neSidelength) % neSidelength,
  		index / neSidelength / neSidelength
  	) - blocksInChunkDim;
  }

  struct NE { ivec3 coord; uint capacity; };
  NE neFromChunk(const int chunkIndex, const int someindex) { 
	const int neIndex = someindex % neCapacity;
	const int chunkOffset = chunkIndex * 16;
	const int neOffset = neIndex / 2;
	const int neShift = (neIndex % 2) * 16;
	
	const uint first = emitters.data[chunkOffset];
	const uint ne16 = (emitters.data[chunkOffset + 1 + neOffset] >> neShift) & 0xffffu;
	
	const uint ne_index = ne16 | (((first >> (2 + neIndex))&1u)<<16);
	const ivec3 ne_coord = neIndexToCoord(int(ne_index));
	
	return NE(ne_coord, first & 3u);
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
	ivec3 intersectionSide;
	int bias;
	uint blockId;
};

IntersectionInfo noIntersectionInfo() {
	IntersectionInfo i;
	i.blockId = 0;
	return i;
}

bvec3 and3b/*seems that glsl has no && for bvec_*/(const bvec3 a, const bvec3 b) { return bvec3(a.x && b.x, a.y && b.y, a.z && b.z); }

vec3 calcDirSign(const vec3 dir) {
	return vec3(greaterThanEqual(dir, vec3(0))) * 2 - 1;/*
		dirSign components must not be 0, because isInters relies on the fact that
		someValue * dirSign * dirSign == someValue
		
		sign(dir) can cause the algorithm to go into an infinite loop.
		This for example happens when trying go compute ray-to-emitter intersection
		when starting position is on the same x, y, or z coordinate as the emitter.
	*/
}

vec2 blockUv(const vec3 localBlockCoord, const vec3 intersectionSide, const vec3 dirSign) {
	const float intersectionUSign = dot(dirSign, intersectionSide);
	
	const vec2 uv = vec2(
		dot(intersectionSide, vec3(localBlockCoord.zx, 1-localBlockCoord.x)),
		dot(intersectionSide, localBlockCoord.yzy)
	);
	
	return vec2(
		mix(uv.x, 1 - uv.x, (intersectionUSign+1) / 2),
		uv.y
	);
}

bool alphaTest/*isSolid*/(const vec3 atPosition, const vec3 intersectionSide, const vec3 dirSign, const vec3 blockCoord, const uint blockId) {
	const vec3 localBlockCoord = atPosition - blockCoord;
	const vec2 uv = blockUv(localBlockCoord, intersectionSide, dirSign);
	
	const float index = dot(floor(atPosition), vec3(7, 13, 17));
	const float cindex = mod(index, 4);
	const float sindex = mod(1 - index, 4);//sinA = cos(90deg - A)
	const mat2 rot = mat2(							
		/*cos(floor(index) * 90deg) <=> index: 0->1, 1->0, 2->-1, 3->0*/
		(1 - cindex) * float(!(cindex == 3)),
		-(1 - sindex) * float(!(sindex == 3)),
		(1 - sindex) * float(!(sindex == 3)),
		(1 - cindex) * float(!(cindex == 3))
	); //some multiple 90 degree rotation per block coord
	const vec2 alphaUv = rot * (uv-0.5) + 0.5; //rotation around (0.5, 0.5)
	
	return sampleAtlas(alphaAt(blockId), alphaUv).x > 0.5;
}


const int minBias = -2;
const int maxBias = +2; /*
	bias allowes to specify which surface is intersected
	
	ray direction ->
				 from cube                           to cube
	 (cube 1 liquid) | (cube 1 solid) | (cube 2 solid) | (cube 2 liquid)
	^                 ^                ^                ^                ^
   -2                -1                0                1                2
   
   it is useful when translucent or parcially transparent cube is ajacent to some other cube
   because we can specify with whith of those the ray has intersectted first
*/

IntersectionInfo isInters(const Ray ray, const int startBias) { 
	const vec3 dir = ray.dir;
	const vec3 dirSign = calcDirSign(dir);
	const vec3 positive_ = max(+dirSign, vec3(0));
	const vec3 negative_ = max(-dirSign, vec3(0));

	const vec3 stepLength = 1 / abs(dir);
	
	Cubes fromBlock;
	int curChunkIndex = chunkAt(blockChunk(ivec3(floor(ray.orig))));
	int bias = startBias;
	vec3 curCoord = ray.orig;
	ivec3 relativeToChunk = ivec3(floor(curCoord / blocksInChunkDim));

	{ //calculate first intersection
		const ivec3 startBlockCoord = ivec3(floor(curCoord) - positive_ * vec3(equal(curCoord, floor(curCoord))));
		const int startBlockNeighbourChunk = chunkAt(blockChunk(startBlockCoord));
		fromBlock = startBlockNeighbourChunk == 0 ? cubesEmpty() : cubesAtBlock(
			startBlockNeighbourChunk,
			blockLocalToChunk(startBlockCoord)
		);
	}
	
	while(true) {
		if(any(greaterThan((relativeToChunk - mix(vec3(0), vec3(renderDiameter-1), positive_)) * dirSign, ivec3(0)))) break;
		
		const vec3 relativeToBlock = vec3(relativeToChunk * blocksInChunkDim);
		//calculate next chunk
		const vec3 relativeCoord = curCoord - relativeToBlock;
		const vec3 farBordersDiff = mix(relativeCoord, blocksInChunkDim - relativeCoord, positive_);
		const vec3 farBordersLen = farBordersDiff*stepLength;
		
		const float farBordersMinLen = min(min(farBordersLen.x, farBordersLen.y), farBordersLen.z); //minimum length for any ray axis to get outside of chunk bounds
		const bvec3 firstOut = equal(farBordersLen, vec3(farBordersMinLen));
		const ivec3 outNeighbourDir = ivec3(firstOut) * ivec3(dirSign);
		
		const int nextChunkIndex = chunkAt(relativeToChunk + outNeighbourDir);
		const ivec3 nextRelativeToChunk = relativeToChunk + outNeighbourDir;
		
		//calculate first intersection with current chunk
		const uint bounds = chunkBounds(curChunkIndex);
		const vec3 startBorder = vec3(start(bounds));
		const vec3 endBorder   = vec3(onePastEnd(bounds));
		const bool empty = emptyBoundsF(startBorder, endBorder);
		
		const vec3 nearBoundaries = mix(startBorder, endBorder, negative_);
		const vec3 farBoundaries  = mix(startBorder, endBorder, positive_);
		
		const vec3 nearBoundsCoords = nearBoundaries + relativeToBlock;
		const vec3 farBoundsCoords  = farBoundaries  + relativeToBlock;
		
		const bool pastNearBounds = all(greaterThanEqual((curCoord - nearBoundsCoords)*dirSign, vec3(0)));
		const vec3  nearBoundsLen = (nearBoundsCoords - ray.orig) * dirSign * stepLength;
		const float nearBoundsMinLen = max(max(nearBoundsLen.x, nearBoundsLen.y), nearBoundsLen.z);  //minimum length for all ray axis to get inside of chunk bounds
		const bvec3 nearBoundCandMinB = equal(nearBoundsLen, vec3(nearBoundsMinLen));
		
		const vec3 candCoord = pastNearBounds ? curCoord : mix(at(ray, nearBoundsMinLen), nearBoundsCoords, nearBoundCandMinB);
		const bvec3 inChunkBounds = lessThanEqual((candCoord - farBoundsCoords) * dirSign, vec3(0,0,0));
		
		if(!all(inChunkBounds) || empty) {
			curChunkIndex = nextChunkIndex;
			relativeToChunk = nextRelativeToChunk;
			continue;
		}
		
		const vec3 candFromBlockCoord = floor(candCoord) - positive_ * vec3(equal(candCoord, floor(candCoord)));
		const vec3 curFromBlockCoord  = floor(curCoord ) - positive_ * vec3(equal(curCoord , floor(curCoord) ));
		
		if(candFromBlockCoord != curFromBlockCoord) fromBlock = cubesEmpty();
		if(candCoord != curCoord) bias = minBias;
		
		curCoord = candCoord;
		
		//step through blocks/cubes of the current chunk
		while(true) {
			const ivec3 intersectionSide = ivec3(equal(curCoord*cubesInBlockDim, floor(curCoord*cubesInBlockDim)));
					
			const ivec3 fromCubeCoord = ivec3(floor(curCoord * cubesInBlockDim - positive_ * vec3(intersectionSide)));
			const ivec3 toCubeCoord   = ivec3(floor(curCoord * cubesInBlockDim - negative_ * vec3(intersectionSide)));
			
			const ivec3 toBlockCoord = cubeBlock(toCubeCoord);
			const bool toBlockInCurChunk = cubeChunk(toCubeCoord) == relativeToChunk;
			const ivec3 localToBlockCoord = blockLocalToChunk(toBlockCoord);
			
			Cubes toBlock;
			
			//const bvec3 blockBounds = equal(curCoord, floor(curCoord));
			//const bool atBlockBounds = any(blockBounds);
			//if(!atBlockBounds) toBlock = fromBlock;
			//else 
			toBlock = cubesAtBlock(
				toBlockInCurChunk ? curChunkIndex : nextChunkIndex,
				localToBlockCoord
			);

			if(!isEmpty(fromBlock) || !isEmpty(toBlock)) {
				const bvec4 cubes = bvec4(
					blockCubeAt (fromBlock, cubeLocalToBlock(fromCubeCoord)),
					liquidCubeAt(fromBlock, cubeLocalToBlock(fromCubeCoord)),
					blockCubeAt (toBlock  , cubeLocalToBlock(toCubeCoord  )),
					liquidCubeAt(toBlock  , cubeLocalToBlock(toCubeCoord  ))
				);
				
				if(any(cubes)) {
					uint fBlockId = 0;
					uint fLiquidId = 0;
					
					uint tBlockId = 0;
					uint tLiquidId = 0;
					
					IntersectionInfo i = noIntersectionInfo();
					
					if(cubes.x) {
						const ivec3 fromBlockChunk = cubeChunk(fromCubeCoord);
						fBlockId = blockIdAt(chunkAt(fromBlockChunk), blockLocalToChunk(cubeBlock(fromCubeCoord)));
					}
					if(cubes.y) {
						const ivec3 fromCubeChunk = cubeChunk(fromCubeCoord);
						const ivec3 fromCubeInChunk = cubeLocalToChunk(fromCubeCoord);
						const LiquidCube fLiquid = liquidAtCube(chunkAt(fromCubeChunk), fromCubeInChunk);
						
						const uint liquidLevel = level(fLiquid);
						
						const float levelY = (fromCubeCoord.y + max((liquidLevel+1) / 16, 1) / 16.0) / cubesInBlockDim; /*
							16 levels in a cube, 32 in a block
						*/
						const float yLevelDiff = levelY - curCoord.y;
						
						if(liquidLevel == 255u || (intersectionSide.xz != 0 && yLevelDiff >= 0) || (intersectionSide.y != 0 && yLevelDiff >= 0))
							fLiquidId = id(fLiquid);
					}
					
					if(cubes.z) {
						tBlockId = blockIdAt(
							toBlockInCurChunk ? curChunkIndex : nextChunkIndex, 
							blockLocalToChunk(cubeBlock(toCubeCoord))
						);		
					}
					if(cubes.w) {
						const LiquidCube tLiquid = liquidAtCube(
							toBlockInCurChunk ? curChunkIndex : nextChunkIndex, 
							cubeLocalToChunk(toCubeCoord)
						);
						
						const ivec3 cubeCoord = toCubeCoord;
						const Ray coordRay = Ray(curCoord, ray.dir);
						
						const uint liquidLevel = level(tLiquid);
						const uint liquidId = id(tLiquid);
						
						const float levelY = (cubeCoord.y + max((liquidLevel+1) / 16, 1) / 16.0) / cubesInBlockDim;
						const float yLevelDiff = levelY - coordRay.orig.y;
						
						if(liquidLevel == 255u) tLiquidId = liquidId;
						else if((intersectionSide.xz != 0 && yLevelDiff >= 0) || (intersectionSide.y != 0 && yLevelDiff >= 0))
							tLiquidId = liquidId;
						
						{
							const float yLevelDist = yLevelDiff * dirSign.y;
							
							if(intersectionSide == 0 && bias <= 0 && yLevelDiff > 0) return IntersectionInfo(
								coordRay.orig,
								coordRay.orig - cubeBlock(cubeCoord),
								ivec3(0, 0, 0),
								0,
								liquidId
							);
							else if(liquidLevel == 255u);
							else if(yLevelDist >= 0) {
								vec3 intersectionCoord;
								intersectionCoord.xz = at(coordRay, yLevelDist * stepLength.y).xz;
								intersectionCoord.y = levelY;
								
								const int liquidBias = dirSign.y > 0 ? -1 : 0;
								
								const bool isBias = (coordRay.orig.y != levelY || bias <= liquidBias);
								
								const vec3 cubeCoordF = cubeCoord;
								if(isBias && intersectionCoord == clamp(intersectionCoord, cubeCoordF / cubesInBlockDim, (cubeCoordF+1) / cubesInBlockDim)) 
									i = IntersectionInfo(
										intersectionCoord,
										intersectionCoord - cubeBlock(cubeCoord),
										ivec3(0, 1, 0),
										liquidBias,
										liquidId
									);
							}
						}
					}

					uint/*uint16_t[4]*/ blocks[2] = { fLiquidId | (fBlockId << 16), tBlockId | (tLiquidId << 16)  };
					const ivec3 blocksCoord[2] = { cubeBlock(fromCubeCoord), cubeBlock(toCubeCoord) };
					
					uint lowestIntersectionId = 0;
					int lowestBias;
					
					for(int surface = maxBias-1; surface >= minBias; surface--) {
						const uint blockIndex = surface - minBias;
						const uint blockId = (blocks[blockIndex/2] >> (16 * (blockIndex%2))) & 0xffffu;
						const vec3 blockCoord = blocksCoord[int(surface >= 0)];
						
						if(blockId == 0) continue; 
						if(intersectionSide == 0 || !alphaTest(curCoord, vec3(intersectionSide), dirSign, blockCoord, blockId)) continue;
						if(blockId == lowestIntersectionId && (blockId == 7 || blockId == 15)) { lowestIntersectionId = 0; continue; }
						if(lowestIntersectionId != 0 && lowestIntersectionId != 15 && blockId == 15) continue; //ignore water backside
						
						if(surface < bias) continue;
						lowestIntersectionId = blockId;
						lowestBias = surface;		
					}
					
					if(lowestIntersectionId != 0) {
						const vec3 blockCoord = blocksCoord[int(lowestBias >= 0)];
						const vec3 localBlockCoord = curCoord - blockCoord;
						
						return IntersectionInfo(
							curCoord,
							localBlockCoord,
							intersectionSide,
							lowestBias,
							lowestIntersectionId
						);
					}
					else if(i.blockId != 0) return i;
				}		
			}
			
			//calculate next intersection
			const bool hnn = hasNoNeighbours(toBlock) || neighboursFullSameLiquid(toBlock);
			const float skipDistance = (hnn || (isEmpty(toBlock) || fullSameLiquid(toBlock))) ? 1.0 : cubesInBlockDim;
			const vec3 candCoords = floor(curCoord * skipDistance * dirSign + (hnn ? 2 : 1)) / skipDistance * dirSign;
	
			const vec3 nextLenghts = (candCoords - ray.orig) * dirSign * stepLength;
			
			const float nextMinLen = min(min(nextLenghts.x, nextLenghts.y), nextLenghts.z);
			const bvec3 nextMinAxisB = equal(nextLenghts, vec3(nextMinLen));
			
			curCoord = mix(
				max(curCoord*dirSign, at(ray, nextMinLen)*dirSign)*dirSign, 
				candCoords, 
				nextMinAxisB
			);
			bias = minBias;
			fromBlock = toBlock;

			const bool inChunkBounds = all(lessThanEqual((curCoord - farBoundsCoords) * dirSign, vec3(0,0,0)) );
		
			if(!inChunkBounds) {
				curChunkIndex = nextChunkIndex;
				relativeToChunk = nextRelativeToChunk;
				break;
			}
		}
	}
	
	return noIntersectionInfo();
}

vec3 background(const vec3 dir) {
	const float t = 0.5 * (dir.y + 1.0);
	const vec3 res = (1.0 - t) * vec3(1.0, 1.0, 1.02) + t * vec3(0.5, 0.7, 1.0);
	return pow(res*2, vec3(2.2));
}

const int rayTypeStandard = 0;
const int rayTypeShadowBlock = 1;
const int rayTypeShadowSky = 2;
const int rayTypeLightingBlock = 3;
const int rayTypeLightingEmitter = 4;

struct Params {
	Ray ray;
	int bias;
	
	uint rayIndex;
	uint rayType;
	int parent;
	bool last;
}; 

struct Result {
	vec3 color;
	float depth;
	uint data;
	uint surfaceId;
	
	uint rayIndex;
	uint rayType;
	int parent;
	bool last;
};

int putResultPos = 0;
int putParamsPos = 0;

const int size = 40;
uint stack[size]; 
//add results ->              <- add params
//result | result | 0 | 0 | params | params;
//   putResultPos ^   ^ putParamsPos

int curResultPos() { return putResultPos - 1; }
void setCurResultPos(const int pos) { putResultPos = pos + 1; }

int paramsPosOffset(const int pos) { return size - (pos+1) * 7; }
int resultPosOffset(const int pos) { return pos * 4; }

void writeParams(const Params it, const int position) {
	const int offset = paramsPosOffset(position);
	
	stack[offset+0] = floatBitsToUint(it.ray.orig.x);
	stack[offset+1] = floatBitsToUint(it.ray.orig.y);
	stack[offset+2] = floatBitsToUint(it.ray.orig.z);
	
	stack[offset+3] = floatBitsToUint(it.ray.dir.x);
	stack[offset+4] = floatBitsToUint(it.ray.dir.y);
	stack[offset+5] = floatBitsToUint(it.ray.dir.z);
	
	stack[offset+6] = (uint(it.bias - minBias) & 0xfu) | ((it.rayIndex & 7u) << 16) | ((it.rayType & 7u) << 19) | ((uint(it.parent) & 0xffu) << 22) | (uint(it.last) << 30);
}
Params readParams(const int position) {
	const int offset = paramsPosOffset(position);
		
	return Params(
		Ray(
			vec3(
				uintBitsToFloat(stack[offset+0]),
				uintBitsToFloat(stack[offset+1]),
				uintBitsToFloat(stack[offset+2])
			),
			vec3(
				uintBitsToFloat(stack[offset+3]),
				uintBitsToFloat(stack[offset+4]),
				uintBitsToFloat(stack[offset+5])
			)
		),
		int((stack[offset+6] >> 0) & 0xfu) + minBias,
		(stack[offset+6] >> 16) & 7u,
		(stack[offset+6] >> 19) & 3u,
		(int(stack[offset+6] << 2) >> (22+2)), //sign extended shift
		bool((stack[offset+6] >> 30) & 1u)
	);
}

void writeResult(const Result it, const int position) {
	const int offset = resultPosOffset(position);
	
	stack[offset+0] = packHalf2x16(it.color.xy);
	stack[offset+1] = packHalf2x16(vec2(it.color.z, it.depth));
	
	stack[offset+2] = it.data;
	stack[offset+3] = (it.surfaceId & 0xffffu) | ((it.rayIndex & 7u) << 16) | ((it.rayType & 7u) << 19) | ((uint(it.parent) & 0xffu) << 22) | (uint(it.last) << 30);
}
Result readResult(const int position) {
	const int offset = resultPosOffset(position);
	
	const vec2 v1 = unpackHalf2x16(stack[offset+0]);
	const vec2 v2 = unpackHalf2x16(stack[offset+1]);
	return Result(
		vec3(
			v1.x,
			v1.y,
			v2.x
		),
		v2.y,
		stack[offset+2],
		(stack[offset+3] >> 0) & 0xffffu,
		(stack[offset+3] >> 16) & 7u,
		(stack[offset+3] >> 19) & 7u,
		(int(stack[offset+3] << 2) >> (22+2)), //sign extended shift
		bool((stack[offset+3] >> 30) & 1u)
	);
}

Params popParams()  { return readParams(--putParamsPos); }
void pushParams(const Params it) { writeParams(it, putParamsPos++); }
void pushResult(const Result it) { writeResult(it, putResultPos++); }

bool canPopParams() { return putParamsPos > 0; }
bool canPushParams() { 
	return paramsPosOffset(putParamsPos) >= 0
	&& resultPosOffset(putResultPos) <= paramsPosOffset(putParamsPos); //resultOnePastEnd <= paramsNextEnd
}
bool canPushParamsCount(const int count) { 
	return paramsPosOffset(putParamsPos + count-1) >= 0
	&& resultPosOffset(putResultPos) <= paramsPosOffset(putParamsPos + count-1); //resultOnePastEnd <= paramsNextEnd
}
bool canPushResult() { 
	return resultPosOffset(putResultPos+1)-1 < size
	&& resultPosOffset(putResultPos+1)-1 < paramsPosOffset(putParamsPos-1); //resultEnd < paramsEnd
}

vec3 fade(const vec3 color, const float depth) {
	const float bw = 0.5 + dot(color, vec3(0.299, 0.587, 0.114)) * 0.5;
	return mix(vec3(bw), color, inversesqrt(depth / 500 + 1));
}

int mapInteger(int value) {//based on https://stackoverflow.com/a/24771093/18704284
    value *= 1664525;
    value += 101390223;
    value ^= value >> 11;
    value ^= value << 16;
    value ^= value >> 23;
    value *= 110351245;
    value += 12345;

    return value;
}

void combineSteps(const int currentIndex, const int lastIndex) {
	const int childrenCount = lastIndex - currentIndex;
	if(childrenCount <= 0) return;
	
	const Result current = readResult(currentIndex);
	Result result = current;
	result.color = vec3(0);
	
	const bool isShadow = current.rayType == rayTypeShadowBlock;
	const bool isLighting = current.rayType == rayTypeLightingBlock;
	const uint surfaceId = current.surfaceId;
	
	if(surfaceId == 7 || surfaceId == 15) {
		const bool glass = surfaceId == 7;
		
		const float fresnel = unpackHalf2x16(current.data).x;
		const bool backside = bool(current.data >> 16);
		const bool both = childrenCount == 2;
		
		for(int i = 0; i < childrenCount; i++) {
			const Result inner = readResult(currentIndex + i+1);
			
			if(isShadow) {
				if(inner.rayType == rayTypeShadowSky) result.rayType = rayTypeShadowSky;
				else continue;
			}			
			if(isLighting) {
				if(inner.rayType == rayTypeLightingEmitter) result.rayType = rayTypeLightingEmitter;
				else continue;
			}
			
			if(inner.rayIndex != 3 && inner.rayIndex != 4) discard;
			const bool reflect = inner.rayIndex == 3;
			
			const vec3 innerCol = inner.color * (both ? (reflect ? fresnel : 1 - fresnel) : 1.0) * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0);
			const vec3 waterInnerCol = ( backside ^^ reflect ? innerCol : mix(current.color, innerCol, exp(-inner.depth)) );
			
			result.color += glass ? (current.color * innerCol) : ( reflect ? mix(current.color, waterInnerCol, 0.97) : waterInnerCol );
		}
	}
	else if(surfaceId == 8) {
		for(int i = 0; i < childrenCount; i++) {
			const Result inner = readResult(currentIndex + i+1);
			if(isShadow) {
				if(inner.rayType == rayTypeShadowSky) result.rayType = rayTypeShadowSky;
				else continue;
			}
			if(isLighting) {
				if(inner.rayType == rayTypeLightingEmitter) result.rayType = rayTypeLightingEmitter;
				else continue;
			}
			result.color = current.color * inner.color * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0);
		}
	}
	else {
		int i = 0;
				
		if(current.rayType == rayTypeStandard) {
			if(i < childrenCount) { //emitter
				const Result inner = readResult(currentIndex + i+1);
				
				if(inner.rayType == rayTypeLightingBlock || inner.rayType == rayTypeLightingEmitter) {
					result.color += inner.rayType == rayTypeLightingEmitter ? (inner.color / (1 + inner.depth*inner.depth)) : vec3(0);
					i++;
				}
			}
			
			if(i < childrenCount) { //shadow
				const Result inner = readResult(currentIndex + i+1);
				
				if(inner.rayType == rayTypeShadowBlock || inner.rayType == rayTypeShadowSky) {
					const vec3 shadowTint = inner.rayType == rayTypeShadowSky ? inner.color : vec3(0.0);
					result.color += current.color * shadowTint;
					i++;
				}
			}
		}
		
		if(surfaceId == 5 && i < childrenCount) {		
			const Result inner = readResult(currentIndex + i+1);
			
			if(inner.rayIndex == 1) {
				if(inner.rayType == rayTypeShadowSky) {
					result.rayType = rayTypeShadowSky;
					result.color += mix(current.color, inner.color, 0.2);
				}
				else if(inner.rayType == rayTypeLightingEmitter) {
					result.rayType = rayTypeLightingEmitter;
					result.color += mix(current.color, inner.color * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0), 0.2);
				}
				else if(inner.rayType == rayTypeLightingBlock || inner.rayType == rayTypeShadowBlock) /*do nothing*/;
				else result.color += mix(current.color, inner.color, 0.03);
				
				i++;
			}
			else result.color += current.color;
		}
		else result.color += current.color;
	}
	
	writeResult(result, currentIndex);
}

struct RayResult {
	bool pushedRays;
};

RayResult traceStep(const int iteration) {
	bool pushed = false;
	
	const int curFrame = putResultPos;
	
	const Params p = popParams();
	//if(!canPushResult()) ???; //this check is not needed because sizeof(Result) < sizeof(Params), so we would have space for at least 1 Result
	const Ray ray = p.ray;
	const vec3 dirSign = calcDirSign(ray.dir);
	
	const uint rayIndex = p.rayIndex;
	const uint type = p.rayType;
	const int startBias = p.bias;
	const int parent = p.parent;
	const bool last = p.last;
	
	const bool shadow = type == rayTypeShadowBlock || type == rayTypeShadowSky || type == rayTypeLightingBlock || type == rayTypeLightingEmitter;
	
	const vec3 startP = playerRelativePosition - vec2(playerWidth/2.0, 0           ).xyx;
	const vec3 endP   = playerRelativePosition + vec2(playerWidth/2.0, playerHeight).xyx;
	
	const IntersectionInfo i = isInters(ray, startBias);
	
	const Intersection playerIntersection = intersectCube(ray, startP, endP, vec3(1,0,0), vec3(0,1,0));
	const bool isPlayer = (drawPlayer || (iteration != 0 && playerIntersection.frontface)) && playerIntersection.is;
	const float playerTsq = dot(playerIntersection.at - ray.orig, playerIntersection.at - ray.orig);
	const bool playerFrist = isPlayer && dot(i.coord - ray.orig, i.coord - ray.orig) > playerTsq;
	
	if(i.blockId != 0 && !playerFrist) { //block
		//if(exit_) return RayResult(false);
		const vec3 coord = i.coord;        
		const vec3 localSpaceCoord = i.localSpaceCoord;
		const ivec3 chunkPosition = ivec3(floor(coord / blocksInChunkDim));
		const int intersectionChunkIndex = chunkAt(chunkPosition);
		const int bias = i.bias;
		const uint blockId = i.blockId;
		const ivec3 intersectionSide = i.intersectionSide;
		const bvec3 intersectionSideB = bvec3(intersectionSide);
		
		const ivec3 side = intersectionSide * (bias >= 0 ? -1 : 1) * ivec3(sign(ray.dir));
		const bool backside = bias < 0;
		const ivec3 normal = side * ( backside ? -1 : 1 );
		
		const vec2 uvAbs = blockUv(coord, intersectionSide, dirSign);
		const vec2 uv    = mod(uvAbs, 1);
		
		const vec2 atlasOffset = atlasAt(blockId, side);
		
		const float t = length(coord - ray.orig);
		
		float light = 1;
		float ambient = 1;
		
		if(!shadow) {				
			const ivec3 otherAxis1 = intersectionSideB.x ? ivec3(0,1,0) : ivec3(1,0,0);
			const ivec3 otherAxis2 = intersectionSideB.z ? ivec3(0,1,0) : ivec3(0,0,1);
			const ivec3 vertexCoord = ivec3(floor(coord * cubesInBlockDim)) - chunkPosition * cubesInChunkDim;
			
			ambient = 0;
			if(!backside) {
				#if 0
				ambient = lightForChunkVertexDir(intersectionChunkIndex, chunkPosition, vertexCoord, normal);
				#else
				for(int i = 0 ; i < 4; i++) {
					const ivec2 offset_ = ivec2(i%2, i/2);
					const ivec3 offset = offset_.x * otherAxis1 + offset_.y * otherAxis2;
					const ivec3 curVertexCoord = vertexCoord + offset;
										
					const float vertexLight = lightForChunkVertexDir(intersectionChunkIndex, chunkPosition, curVertexCoord, normal);
					
					const float diffX = abs(offset_.x - mod(dot(localSpaceCoord, otherAxis1)*2, 1));
					const float diffY = abs(offset_.y - mod(dot(localSpaceCoord, otherAxis2)*2, 1));
					ambient += mix(mix(vertexLight, 0, diffY), 0, diffX);
				}
				#endif
			}
			else ambient = 0.5;
			
			#if 0
			ambient -= 0.7;
			ambient *= 3;
			#endif
			
			const ivec3 dirSignI = ivec3(sign(ray.dir));
			const ivec3 positive_ = max(dirSignI, ivec3(0));
			const ivec3 negative_ = max(-dirSignI, ivec3(0));
			
			light = max(
				float(bias >= 0) * lightingAtCube(intersectionChunkIndex, chunkPosition, vertexCoord - negative_ * intersectionSide),
				float(bias <= 0) * lightingAtCube(intersectionChunkIndex, chunkPosition, vertexCoord - positive_ * intersectionSide)
			);
		}

		if(blockId == 7 || blockId == 15) {
			const bool glass = blockId == 7;
			
			const float offsetMag = clamp(t * 50 - 0.001, 0, 1);
			const vec3 glassOffset = vec3( 
				sin(localSpaceCoord.x*9)/2, 
				sin(localSpaceCoord.z*9)/8, 
				sin(localSpaceCoord.y*6 + localSpaceCoord.x*4)/8
			) / 100;
			const vec3 waterOffset = (texture(noise, uvAbs / 5  + (texture(noise, uvAbs/30 + time/60).xy-0.5) * 0.8 ).xyz - 0.5) * 0.2;
			const vec3 offset_ = (glass ? glassOffset : waterOffset);
			const vec3 offset = offset_ * offsetMag;
			
			const vec3 newBlockCoord = localSpaceCoord + offset * vec3(1-intersectionSide);
			const vec3 offsetedCoord = mix(
				clamp(coord + offset, floor(coord*cubesInBlockDim)/cubesInBlockDim+0.001, ceil(coord*cubesInBlockDim)/cubesInBlockDim-0.001), 
				coord, 
				intersectionSideB
			);
			
			const vec2 newUV = mod(blockUv(newBlockCoord, intersectionSide, dirSign), 1);
			const vec3 color_ = sampleAtlas(atlasOffset, newUV) * mix(0.5, 1.0, ambient);
			const vec3 color = fade(color_, t);

			vec3 incoming_ = any(intersectionSideB) ? normalize(offsetedCoord - ray.orig) : ray.dir * (1 + offset_);
			if(any(isnan(incoming_))) incoming_ = ray.dir;
			const vec3 incoming = incoming_;
			const vec3 normalDir = any(intersectionSideB) ? normalize(vec3(normal) + offset * vec3(1-intersectionSide)) : -incoming;
			const float ior = glass ? 1.0 : 1.00;
			
			const float n1 = backside ? ior : 1;
			const float n2 = backside ? 1 : ior;
			const float r0 = pow((n1 - n2) / (n1 + n2), 2);
			const float fresnel = r0 + (1 - r0) * pow(1 - abs(dot(incoming, normalDir)), 5);

			const vec3 refracted = refract(incoming, normalDir, n1 / n2);
			
			const bool isRefracted = refracted != vec3(0);
			const bool isReflected = any(intersectionSideB) ? (isRefracted ? !backside && iteration < 3 : true) : false;
			
			const uint data = (packHalf2x16(vec2(fresnel, 0.0)) & 0xffffu) | (uint(backside) << 16);
			
			pushResult(Result( color, t, data, blockId, rayIndex, type, parent, last ));
			
			if(isRefracted && canPushParams()) {
				const vec3 newDir = refracted;
				const int newBias = bias + 1;
				
				pushParams(Params( Ray(offsetedCoord, newDir), newBias, 4u, type, curFrame, !pushed));
				pushed = true;
			}	
			
			if(isReflected && canPushParams()) {
				const vec3 newDir = reflect(incoming, normalDir);
				const int newBias = -bias;
				
				pushParams(Params( Ray(offsetedCoord, newDir), newBias, 3u, type, curFrame, !pushed));
				pushed = true;
			}
		}
		else if(blockId == 8) {
			const vec3 offset = normalize(texture(noise, uv/5).xyz - 0.5) * 0.01;
			
			const vec3 newBlockCoord = localSpaceCoord - offset;
			const vec2 newUv = blockUv(newBlockCoord, intersectionSide, dirSign);
			
			const vec3 color_ = sampleAtlas(atlasOffset, newUv) * light * mix(0.3, 1.0, ambient);
			const vec3 color = fade(color_, t);
			
			
			pushResult(Result( color, t, 0u, blockId, rayIndex, type, parent, last));
			if(canPushParams()) {
				const vec3 reflected_ = reflect( ray.dir, normalize( normal + offset ) );
				const vec3 reflected  = abs(reflected_) * normal + reflected_ * (1 - abs(normal));
			
				pushParams(Params( Ray(coord, reflected), -bias, 0u, type, curFrame, !pushed));
				pushed = true;
			}
		}
		else {
			const bool emitter = blockId == 13 || blockId == 14;
			const ivec3 relativeToChunk = chunkPosition;
			const vec3 color_ = sampleAtlas(atlasOffset, uv) * (emitter ? 1.0 : light * mix(0.3, 1.0, ambient));
			const vec3 color = fade(color_, t);

			pushResult(Result( color * (emitter ? 5 : 1), t, 0u, blockId, rayIndex, (emitter && type == rayTypeLightingBlock) ? rayTypeLightingEmitter : type, parent, last));
			
			if(blockId == 5 && iteration < 3) {
				if(canPushParams()) {
					pushParams(Params( Ray(coord, ray.dir), bias+1, 1u, type, curFrame, !pushed));
					pushed = true;
				}
			}
			if(!shadow && !emitter) {
				const int shadowOffsetsCount = 4;
				const float shadowOffsets[] = { -1, 0, 1, 0 };
				
				const int shadowSubdiv = 16;
				const float shadowSmoothness = 32;
				const int shadowsInChunkDim = blocksInChunkDim * shadowSubdiv;		
				
				const vec3 position = ( floor(coord * shadowSubdiv) + (1-abs(normal))*vec3(0.501, 0.501, 0.501) )/shadowSubdiv;
				
				const ivec3 shadowInChunkCoord = ivec3(floor(mod(coord * shadowSubdiv, vec3(shadowsInChunkDim))));
				const int shadowInChunkIndex = 
					  shadowInChunkCoord.x
					+ shadowInChunkCoord.y * shadowsInChunkDim
					+ shadowInChunkCoord.z * shadowsInChunkDim * shadowsInChunkDim; //should fit in 31 bit
				const int rShadowInChunkIndex = mapInteger(shadowInChunkIndex) / 37;
				const int dirIndex = int(uint(rShadowInChunkIndex) % 42);
				
				if(canPushParams()) { //shadow
					const vec3 offset_ = vec3(
						shadowOffsets[ uint(rShadowInChunkIndex) % shadowOffsetsCount ],
						shadowOffsets[ uint(rShadowInChunkIndex/8) % shadowOffsetsCount ],
						shadowOffsets[ uint(rShadowInChunkIndex/32) % shadowOffsetsCount ]
					);
					const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness;
					const vec4 q = vec4(normalize(vec3(-1-sin(time/4)/10,3,2)), cos(time/4)/5);
					const vec3 v = normalize(vec3(-1, 4, 2) + offset);
					const vec3 temp = cross(q.xyz, v) + q.w * v;
					const vec3 rotated = v + 2.0*cross(q.xyz, temp);
					
					const vec3 newDir = normalize(rotated);
					const int newBias = bias * int(sign(dot(ray.dir*newDir, intersectionSide)));
					
					pushParams(Params( Ray(position, newDir), newBias, 0u, rayTypeShadowBlock, curFrame, !pushed));
					pushed = true;
				}
				
				if(canPushParams()) {
					const NE ne = neFromChunk(intersectionChunkIndex, dirIndex);
					const bool emitters = ne.capacity != 0u && (ne.capacity == 1u ? dirIndex < 30 : true);
					
					if(emitters) {
						//calculate random vector that hits the sphere inside emitter block//
						
						const vec3 relativeCoord = position - relativeToChunk * blocksInChunkDim;
						const vec3 diff = ne.coord+0.4999 - relativeCoord;
						const vec3 dir1 = normalize(diff);
						
						const float angularDiam = 2.0 * asin(1.0 / (0.001 + 2.0 * length(diff))); //angular diameter of a sphere with diameter of 1 block
						
						//https://math.stackexchange.com/a/211195
						vec3 dir2 = vec3(dir1.z, dir1.z, -dir1.x - dir1.y);
						if(abs(dot(dir2, dir2)) < 0.01) dir2 = vec3(-dir1.y-dir1.z,dir1.x,dir1.x);
						dir2 = normalize(dir2);
						const vec3 dir3 = cross(dir1, dir2);
						
						const vec2 random = vec2(rShadowInChunkIndex & 0xfff, int(uint(rShadowInChunkIndex) >> 12) & 0xfff) / 0xfff;
						
						const float twoPi = 6.28318530718;
						const vec2 rotation = (vec2(cos(random.x * twoPi), sin(random.x * twoPi)) * angularDiam/2.0 * random.y)*0.99;/*
							random point inside circle
						*/
						
						const mat3 newDirMatrix = {
							dir3, dir2, -dir1
						}; //forward vector is (0, 0, -1)
						
						const vec3 newDir = newDirMatrix * vec3(cos(rotation.y) * sin(rotation.x), sin(rotation.y), -cos(rotation.y) * cos(rotation.x));
						
						const int newBias = bias * int(sign(dot(ray.dir*newDir, intersectionSide)));
						
						if((abs(dot(newDir, newDir) - 1) < 0.01)) {
							pushParams(Params( Ray(position, newDir), newBias, 0u, rayTypeLightingBlock, curFrame, !pushed));
							pushed = true;
						}
					}
				}
			}
		}
	}
	else if(isPlayer) { //player
		pushResult(Result(vec3(1), sqrt(playerTsq), 0u, 1, rayIndex, type, parent, last));
		if(!shadow && canPushParams()) {
			const vec4 q = vec4(normalize(vec3(1+sin(time/4)/10,3,2)), cos(time/4)/5);
			const vec3 v = normalize(vec3(-1, 4, 2));
			const vec3 temp = cross(q.xyz, v) + q.w * v;
			const vec3 rotated = v + 2.0*cross(q.xyz, temp);
			
			const vec3 newDir = normalize(rotated);
			const Ray newRay = Ray( playerIntersection.at + playerIntersection.normal * 0.001, newDir );

			pushParams(Params(newRay, 0, 0u, rayTypeShadowBlock, curFrame, !pushed));
			pushed = true;
		}
	}
	else { //sky
		pushResult(Result(shadow ? vec3(2) : background(ray.dir), far, 0, 0u, rayIndex, type == rayTypeShadowBlock ? rayTypeShadowSky : type, parent, last));
	}
	
	return RayResult(pushed);
}

vec3 trace(const Ray startRay) {	
	if(!canPushParams()) return vec3(1,0,1);
	pushParams(Params(startRay, 0, 0u, rayTypeStandard, curResultPos(), true));
	
	int iteration = 0;
	while(true) {
		const bool lastRay = !canPopParams();
		if(!lastRay) {
			const RayResult result = traceStep(iteration);
			//if(exit_) return vec3(0);
			iteration++;
			if(result.pushedRays) continue;
		}

		bool stop = false;
		int curFrame = curResultPos();
		for(;curFrame >= 0;) {
			const Result currentResult = readResult(curFrame);
			const bool last = currentResult.last;
			const int currentParent = currentResult.parent;
			if(currentParent < 0) { stop = true; break; }
			if(!last && !lastRay) break;

			combineSteps(currentParent, curFrame);
			curFrame = currentParent;
		}
		setCurResultPos(curFrame);
		if(stop) break;
	}
		
	if(curResultPos() < 0) return vec3(1, 0, 1);

	return readResult(0).color;
}

vec3 colorMapping(const vec3 col) {
	const float bw = 0.299 * col.r + 0.587 * col.g + 0.114 * col.b;
	
	const vec3 c = rgb2hsv(col);
	return hsv2rgb(vec3(
		c.x,
		(c.y+0.03) / pow(c.z+1.0, 1.0 / 25),
		pow(log(c.z + 1.0) / log(bw + 7.8), 1.25)
	));
}

void main() {
	const vec2 uv = gl_FragCoord.xy / windowSize.xy;
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const Ray ray = Ray(startCoord, rayDir);

	const vec3 col = trace(ray);
	//if(exit_) { color = exitVec3; return; }
	
	const vec3 c2 = colorMapping(col);
	
	if(length(gl_FragCoord.xy - windowSize / 2) < 3) 
		color = vec4(vec3(0.98), 1);
	else 
		color = vec4(c2, 1);
}	