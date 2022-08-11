#version 460

#define DEBUG 1

/**
	TODO: figure out how to use multiple sources for shader and how and where to specify #version
	if version is specified only in the first string passed for compilation, then one set of errors occur.
	if version is specified for every string, then this happens:
	
	Compilation error in shader "main shader":
	0(48) : error C0204: version directive must be first statement and may not be repeated
	0(52) : error C7621: #extension directive must occur before any non-preprocessor token
	0(53) : error C7621: #extension directive must occur before any non-preprocessor token
**/

#if DEBUG
#extension GL_ARB_shader_group_vote : enable
#extension GL_ARB_shader_ballot : enable
#endif

//the sign(0) == 0 causes too many problems in this shader, so using it again is probably an error
#define sign0_(TYPE) TYPE sign0(const TYPE it) { return sign(it); }
sign0_(float)
sign0_(vec2)
sign0_(vec3)
sign0_(vec4)
sign0_(int)
sign0_(ivec2)
sign0_(ivec3)
sign0_(ivec4)
#undef sign0_

#define sign(ARG) { int   You_probably_meant_to_use_sign_with_no_zeroes_calcDirSign_If_not_use_sign0; \
					float You_probably_meant_to_use_sign_with_no_zeroes_calcDirSign_If_not_use_sign0; }
//the macro above, when substituted, causes compilation error

bvec3 isDirPositive(const vec3 dir) { return greaterThanEqual(dir, vec3(0)); }
bvec3 isDirNegative(const vec3 dir) { return lessThan        (dir, vec3(0)); }

ivec3 calcDirSign(const vec3 dir) {
	return ivec3(isDirPositive(dir)) * 2 - 1;/*
		dirSign components must not be 0, because intersection calculations rely on the fact that
		someValue * dirSign * dirSign == someValue
		
		sign(dir) can cause shader to go into an infinite loop.
		This for example happens when trying go compute ray-to-emitter intersection
		when starting position is on the same x, y, or z coordinate as the emitter,
		or when ray intersection normal is parallel to ray direction.
	*/
}


in vec4 gl_FragCoord;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 brightColor;

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
	uint flags;
};

uniform vec3 rightDir, topDir;

uniform sampler2D atlas;
uniform float atlasTileSize;

uniform float near;
uniform float far;

uniform sampler2D noise;

uniform ivec3 chunksOffset;
uniform vec3 playerRelativePosition;
uniform bool drawPlayer;
uniform float playerWidth;
uniform float playerHeight;

uniform vec3 startCoord;

uniform int viewDistance;

/*const*/ int renderDiameter = viewDistance*2 + 1;
//ERROR: '=' : global const initializers must be constant ' const int'

#if DEBUG
//these variables are used for debugging purpposes. Like `discard` but allows outputing different colors
bool exit_ = false; 
vec3 exitVec3 = vec3(5,5,5);
#endif

// blocks //
//copied from BlockProperties.h
const uint airBlock = 0u;
const uint grassBlock = 1u;
const uint dirtBlock = 2u;
const uint planksBlock = 3u;
const uint woodBlock = 4u;
const uint leavesBlock = 5u;
const uint stoneBlock = 6u;
const uint glassBlock = 7u;
const uint diamondBlock = 8u;
const uint obsidianBlock = 9u;
const uint rainbowBlock = 10u;
const uint firebrickBlock = 11u;
const uint stoneBrickBlock = 12u;
const uint lamp1Block = 13u;
const uint lamp2Block = 14u;
const uint water = 15u;
const uint grass = 16u;
const uint glassRedBlock = 17u;
const uint glassOrangeBlock = 18u;
const uint glassYellowBlock = 19u;
const uint glassGreenBlock = 20u;
const uint glassTurquoiseBlock = 21u;
const uint glassCyanBlock = 22u;
const uint glassBlueBlock = 23u;
const uint glassVioletBlock = 24u;
const uint glassMagentaBlock = 25u;

const uint blocksCount = 26u;

bool isGlass(const uint id) {
	return id == glassBlock || (id >= glassRedBlock && id <= glassMagentaBlock);
}

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

//copied from Chunks.h 
int cubeIndexInChunk(const ivec3 coord) {
	return coord.x + coord.y*cubesInChunkDim + coord.z*cubesInChunkDim*cubesInChunkDim;
}

ivec3 testBounds(const ivec3 i) {
	#define test(i) ( int(i >= 0) - int(i < blocksInChunkDim) )
		return ivec3(test(i.x), test(i.y), test(i.z));
	#undef ch16
}

bool checkBoundaries(const ivec3 i) {
	return all(equal( testBounds(i), ivec3(0) ));
}

// misc //
const vec3 rgbToLum = vec3(0.2126, 0.7152, 0.0722);

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

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
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

ivec3 mix3i(const ivec3 a, const ivec3 b, const ivec3 f) {
	return (1-f) * a + b * f;
}

int dot3i(const ivec3 a, const ivec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

bvec3 and3b/*seems that glsl has no && for bvecn*/(const bvec3 a, const bvec3 b) { return bvec3(a.x && b.x, a.y && b.y, a.z && b.z); }

struct Ray {
    vec3 orig;
    vec3 dir;
};
vec3 at(const Ray r, const float t) {
    return r.orig + r.dir * t;
}

// AABB //
//copied from Chunk.h PackedAABB
restrict readonly buffer ChunksBounds {
    uint data[];
} aabb;

uint chunkBounds(const int chunkIndex) {
	return aabb.data[chunkIndex];
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

// Chunks indices in the grid //
restrict readonly buffer ChunksIndices {
    int data[];
} chunksIndices;

ivec3 chunkPositionIndexToPos(const int index) {
	return ivec3(
		 index                                    % renderDiameter,
		(index / renderDiameter)                  % renderDiameter,
		(index / renderDiameter / renderDiameter) % renderDiameter	
	);
}
int chunkPosToPositionIndex(const ivec3 coord) {
	return coord.x + coord.y * renderDiameter + coord.z * renderDiameter * renderDiameter;
}

int chunkAtPositionIndex(const int positionIndex) {	
	return chunksIndices.data[positionIndex];
}

int chunkAt(const ivec3 coord) {
	if(clamp(coord, 0, renderDiameter-1) != coord) return 0;
	return chunkAtPositionIndex(chunkPosToPositionIndex(coord));
}

// Textures //
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

// chunks blocks' cubes and neighbours info //
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

// chunks blocks' id //
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

// chunks liquid cubes //
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

	uint data = chunksLiquid.data[index];
	
	if((data >> 24 & 1) != 0) data |= (0xffu << 16); //set level to maximun if liquid is falling down
	
	return LiquidCube(data);
}

////
restrict readonly buffer ChunksAO {
    uint data[];
} ao;
  
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

float aoForChunkVertexDir(const int chunkIndex, const ivec3 chunkCoord, const ivec3 vertex, const ivec3 dir) {
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

////
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

// player intersection //
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
	vec3 at;
	int side;
	int bias;
	bool is;
};

Intersection intersectCube(const Ray ray, const int bias, const vec3 start, const vec3 end, const vec3 n1, const vec3 n2) {
	const vec3 size = (end - start) / 2 * 0.999;
	const vec3 center = (end + start) / 2; 
	const bool isFrontface = !all(equal(ray.orig, clamp(ray.orig, center - size, center + size)));
	const float frontface = float(isFrontface) * 2 - 1;
    const vec3 n3 = cross(n2, n1);
	
	const vec3 nn[3] = { 
         n1 * -mix(-1, 1, dot(n1, ray.dir) > 0) * frontface
        ,n2 * -mix(-1, 1, dot(n2, ray.dir) > 0) * frontface
        ,n3 * -mix(-1, 1, dot(n3, ray.dir) > 0) * frontface
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
	int shortestI = 0;
	for (int i = 0; i < sides; i++) {
		float t = arr[i];
		if (t >= 0 && t < shortestT) {
			shortestT = t;
			shortestI = i;
		}
	}
	
	const vec3 normal = nn[shortestI];
	const vec3 intersectionCoord = mix(center + normal * size, at(ray, shortestT), equal(normal, vec3(0)));
	const int newBias = dot(normal, ray.dir) >= 0 ? -1 : 0;
	const bool isBias = (shortestT > 0.0001 || bias <= newBias);
	
	if(isBias) return Intersection(
		intersectionCoord,
		shortestI,
		newBias,
		shortestT < 1.0 / 0.0
	);
	else {
		Intersection i;
		i.is = false;
		return i;
	}
}

////
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
	const vec2 uv_ = blockUv(atPosition, intersectionSide, dirSign);
	
	vec2 alphaUv;
	if(blockId == 5) {
		const vec2 offset = sin(uv_ + time*1.31);
		
		const vec3 posOffseted = atPosition + vec3(
			intersectionSide.x == 1 ? 0 : offset.x, 
			intersectionSide.y == 1 ? 0 : offset.y, 
			intersectionSide.z == 1 ? 0 : (intersectionSide.x == 1 ? offset.x : offset.y) 
		)*0.01;
		
		const vec2 uv = blockUv(posOffseted, intersectionSide, dirSign);
		
		const float index = dot(floor(posOffseted), vec3(7, 13, 17));
		const float cindex = mod(index, 4);
		const float sindex = mod(1 - index, 4);//sinA = cos(90deg - A)
		const mat2 rot = mat2(//	1, 0, 0, 1);					
			/*cos(floor(index) * 90deg) <=> index: 0->1, 1->0, 2->-1, 3->0*/
			(1 - cindex) * float(!(cindex == 3)),
			-(1 - sindex) * float(!(sindex == 3)),
			(1 - sindex) * float(!(sindex == 3)),
			(1 - cindex) * float(!(cindex == 3))
		); //some multiple 90 degree rotation per block coord
		alphaUv = rot * (uv-0.5) + 0.5; //rotation around (0.5, 0.5)
	}
	else alphaUv = uv_;
	
	return sampleAtlas(alphaAt(blockId), mod(alphaUv, 1)).x > 0.5;
}

// bias //
const int minBias = -2;
const int maxBias = +2; /*
	bias allowes to specify which surface is intersected
	
	ray direction ->
				 from cube                           to cube
	  (cube 1 liquid)   (cube 1 solid)   (cube 2 solid)   (cube 2 liquid)  
	^                 ^                ^                ^                 ^
   -2                -1                0                1                 2
   
   it is useful when translucent or parcially transparent cube is ajacent to some other cube
   because we can specify with whith of those the ray has intersectted first.
   
   Bias also acts as backside/frontside indicator when there is only one surface.
*/

// rays and results //
const int rayTypeStandard = 0;
const int rayTypeShadowBlock = 1;
const int rayTypeShadowSky = 2;
const int rayTypeLightingBlock = 3;
const int rayTypeLightingEmitter = 4;

struct Params {
	Ray ray;
	float distance; //TODO: ray length can be stored as the length of the dirrection
	int bias;
	uint id; //16 bits
	uint data; //only 12 bits!
	uint rayId; //0-8 //there can be more than 8 rays, because duplicate ids are allowed. 
	uint rayType;
	
	int parent; //0-255
	bool last;
};

struct Result {
	vec3 color;
	float depth;
	uint data;
	uint surfaceId;
	uint rayId;
	uint rayType;
	
	int parent;
	bool last;
};

const int sizeofParams = 8;
const int sizeofResult = 4;

// params and results stack //
int putResultPos = 0;
int putParamsPos = 0;

const int size = 48;
uint stack[size]; 
//add results ->              <- add params
//result | result | 0 | 0 | params | params;
//   putResultPos ^   ^ putParamsPos

int curParamsPos() { return putParamsPos - 1; }
void setCurParamsPos(const int pos) { putParamsPos = pos + 1; }

int curResultPos() { return putResultPos - 1; }
void setCurResultPos(const int pos) { putResultPos = pos + 1; }

int paramsPosOffset(const int pos) { return size - (pos+1) * sizeofParams; }
int resultPosOffset(const int pos) { return pos * sizeofResult; }

////
restrict buffer TraceTest {
	uint data[];
} traceB;
const int maxParams = 20;

//only one or none can be active at the same time
#define TEST_RESOLVE_COMBINE 0
#define TEST_FIND 0
//only one or none can be active at the same time
#define WRITE_TRACE 0
#define READ_TRACE 0

//ERROR: global const initializers must be constant
/*const*/ ivec3 fragCoord = ivec3(floor(gl_FragCoord));
/*const*/ int maxPosOffset = int((fragCoord.x + fragCoord.y * windowSize.x) * (maxParams * sizeofParams + 1) + maxParams * sizeofParams);
uint getMaxGetPos() { return traceB.data[maxPosOffset]; }
int paramsPosOffsetTrace(const int pos) { return int((fragCoord.x + fragCoord.y * windowSize.x) * (maxParams * sizeofParams + 1) + pos * sizeofParams); }

int setParamsPos = 0;
int getParamsPos = 0;

////
void writeParamsStack(const Params it, const int position) {
	const int offset = paramsPosOffset(position);
	
	stack[offset+0] = floatBitsToUint(it.ray.orig.x);
	stack[offset+1] = floatBitsToUint(it.ray.orig.y);
	stack[offset+2] = floatBitsToUint(it.ray.orig.z);
	
	stack[offset+3] = floatBitsToUint(it.ray.dir.x);
	stack[offset+4] = floatBitsToUint(it.ray.dir.y);
	stack[offset+5] = floatBitsToUint(it.ray.dir.z);
	
	stack[offset+6] = 
		(packHalf2x16(vec2(it.distance, 0)) & 0xffffu)
		| ((it.rayId & 7u) << 16)
		| ((it.rayType & 7u) << 19)
		| ((uint(it.parent) & 0xffu) << 22)
		| (uint(it.last) << 30);
		
	stack[offset+7] = (it.id & 0xffffu) | (((it.bias - minBias) & 0xfu) << 16) | (it.data << 24);
}

void writeParamsTrace(const Params it, const int position) {
	if(setParamsPos < 0 || setParamsPos >= maxParams) discard;
	
	const int offset = paramsPosOffsetTrace(setParamsPos);
	
	if(offset < 0 || offset >= traceB.data.length()) discard;

	traceB.data[offset+0] = floatBitsToUint(it.ray.orig.x);
	traceB.data[offset+1] = floatBitsToUint(it.ray.orig.y);
	traceB.data[offset+2] = floatBitsToUint(it.ray.orig.z);
	
	traceB.data[offset+3] = floatBitsToUint(it.ray.dir.x);
	traceB.data[offset+4] = floatBitsToUint(it.ray.dir.y);
	traceB.data[offset+5] = floatBitsToUint(it.ray.dir.z);
	
	traceB.data[offset+6] = 
		(packHalf2x16(vec2(it.distance, 0)) & 0xffffu)
		| ((it.rayId & 7u) << 16)
		| ((it.rayType & 7u) << 19)
		| ((uint(it.parent) & 0xffu) << 22)
		| (uint(it.last) << 30);
		
	traceB.data[offset+7] = (it.id & 0xffffu) | (((it.bias - minBias) & 0xfu) << 16) | (it.data << 24);

	setParamsPos++;
	traceB.data[maxPosOffset] = setParamsPos;
}

Params readParamsStack(const int position) {
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
		unpackHalf2x16(stack[offset+6]).x,
		int((stack[offset+7] >> 16) & 0xfu) + minBias,
		(stack[offset+7]) & 0xffffu,
		(stack[offset+7] >> 24),
		(stack[offset+6] >> 16) & 7u,
		(stack[offset+6] >> 19) & 7u,
		(int(stack[offset+6] << 2) >> (22+2)), //sign extended shift
		bool((stack[offset+6] >> 30) & 1u)
	);
}

Params readParamsTrace(const int position) {
	if(getParamsPos < 0 || getParamsPos >= getMaxGetPos()) discard;
	
	const int offset = paramsPosOffsetTrace(getParamsPos);
	
	if(offset < 0 || offset >= traceB.data.length()) discard;

	getParamsPos++;
	return Params(
		Ray(
			vec3(
				uintBitsToFloat(traceB.data[offset+0]),
				uintBitsToFloat(traceB.data[offset+1]),
				uintBitsToFloat(traceB.data[offset+2])
			),
			vec3(
				uintBitsToFloat(traceB.data[offset+3]),
				uintBitsToFloat(traceB.data[offset+4]),
				uintBitsToFloat(traceB.data[offset+5])
			)
		),
		unpackHalf2x16(traceB.data[offset+6]).x,
		int((traceB.data[offset+7] >> 16) & 0xfu) + minBias,
		(traceB.data[offset+7]) & 0xffffu,
		(traceB.data[offset+7] >> 24),
		(traceB.data[offset+6] >> 16) & 7u,
		(traceB.data[offset+6] >> 19) & 7u,
		(int(traceB.data[offset+6] << 2) >> (22+2)), //sign extended shift
		bool((traceB.data[offset+6] >> 30) & 1u)
	);
}

void writeStartParams(const Params it, const int position);
void writeEndParams(const Params it, const int position);

Params readStartParams(const int position);
Params readEndParams(const int position);

#if TEST_RESOLVE_COMBINE
	void writeStartParams(const Params it, const int position) {
		writeParamsStack(it, position);
	}
	
	void writeEndParams(const Params it, const int position) {
		writeParamsStack(it, position);
	}

	Params readStartParams(const int position) {
		return readParamsStack(position);
	}

	Params readEndParams(const int position) {
		#if READ_TRACE
		  const Params it = readParamsTrace(position);
		#else
		  const Params it = readParamsStack(position);
		#endif
		
		#if WRITE_TRACE
		  writeParamsTrace(it, position);
		#endif
		
		return it;
	}
#elif TEST_FIND
	void writeStartParams(const Params it, const int position) {
		writeParamsStack(it, position);
		#if WRITE_TRACE
		  writeParamsTrace(it, position);
		#endif
	}
	
	void writeEndParams(const Params it, const int position) {
		#if !(READ_TRACE) //we can read more Params than fits in stack[], so when they are written back position may be out of bounds
		writeParamsStack(it, position);
		#endif
	}

	Params readStartParams(const int position) {
		#if READ_TRACE
		  getParamsPos = position;
		  return readParamsTrace(position);
		#else
		  return readParamsStack(position);
		#endif
	}

	Params readEndParams(const int position) {
		return readParamsStack(position);	
	}
#else
	void writeStartParams(const Params it, const int position) {
		writeParamsStack(it, position);
	}
	
	void writeEndParams(const Params it, const int position) {
		writeParamsStack(it, position);
	}
	
	Params readStartParams(const int position) {
		return readParamsStack(position);
	}
	
	Params readEndParams(const int position) {
		return readParamsStack(position);
	}
#endif

void writeResult(const Result it, const int position) {
	const int offset = resultPosOffset(position);
	
	stack[offset+0] = packHalf2x16(it.color.xy);
	stack[offset+1] = packHalf2x16(vec2(it.color.z, it.depth));
	
	stack[offset+2] = it.data;
	stack[offset+3] = (it.surfaceId & 0xffffu) | ((it.rayId & 7u) << 16) | ((it.rayType & 7u) << 19) | ((uint(it.parent) & 0xffu) << 22) | (uint(it.last) << 30);
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


Params popEndParams()  { return readEndParams(--putParamsPos); }
void pushStartParams(const Params it) { writeStartParams(it, putParamsPos++); }
void pushResult(const Result it) { writeResult(it, putResultPos++); }

bool canPopParams() { return putParamsPos > 0; }
bool canPushParams() { 
	return paramsPosOffset(putParamsPos) >= 0
	&& resultPosOffset(putResultPos) <= paramsPosOffset(putParamsPos); //resultOnePastEnd <= paramsNextEnd
}
bool canPushResult() { 
	return resultPosOffset(putResultPos+1)-1 < size
	&& resultPosOffset(putResultPos+1)-1 < paramsPosOffset(putParamsPos-1); //resultEnd < paramsEnd
}

// intersectuion finding //
struct BlocksIntersection {
	vec3 coord;
	uint id;
	uint data; //only 12 bits!
	int bias;
};

BlocksIntersection blocksIntersection(
	const Ray ray, const bvec3 intersectionSide, const int bias,
	const ivec3 fromCubeCoord, const ivec3 toCubeCoord, const bvec4 cubes, 
	const int fromCubeChunkIndex, const int toCubeChunkIndex
) {
	const bool intersectionOnly = true;
	const bvec3 dirPositive = isDirPositive(ray.dir);
	
	#define dirSign calcDirSign(ray.dir)
	
	uint fBlockId = 0;
	uint fLiquidId = 0;
	
	uint tBlockId = 0;
	uint tLiquidId = 0;
	
	BlocksIntersection bc = BlocksIntersection(ray.orig, 0u, 0u, maxBias);

	#define shouldWriteInterrsection(INTERSECTION_CCORD) (bc.id == 0 || dot((INTERSECTION_CCORD - bc.coord)*dirSign, vec3(intersectionSide)) < 0)
	
	if(cubes.x) {
		const ivec3 fromBlockChunk = cubeChunk(fromCubeCoord);
		fBlockId = blockIdAt(fromCubeChunkIndex, blockLocalToChunk(cubeBlock(fromCubeCoord)));
		
		if(fBlockId == 16) fBlockId = 0;
	}
	if(cubes.y) {
		const ivec3 fromCubeInChunk = cubeLocalToChunk(fromCubeCoord);
		const LiquidCube fLiquid = liquidAtCube(fromCubeChunkIndex, fromCubeInChunk);
		
		const uint liquidLevel = level(fLiquid);
		
		const float levelY = (fromCubeCoord.y + max((liquidLevel+1) / 16, 1) / 16.0) / cubesInBlockDim; /*
			16 levels in a cube, 32 in a block
		*/
		const float yLevelDiff = levelY - ray.orig.y;
		
		if(liquidLevel == 255u || (any(intersectionSide.xz) && yLevelDiff >= 0) || (intersectionSide.y && yLevelDiff >= 0))
			fLiquidId = id(fLiquid);
	}
	
	if(cubes.z) {
		const ivec3 toBlockCoord = cubeBlock(toCubeCoord);
		tBlockId = blockIdAt(
			toCubeChunkIndex,
			blockLocalToChunk(toBlockCoord)
		);
		
		if(tBlockId == 16) {
			const ivec3 toBlockAbsoluteCoord = chunksOffset * blocksInChunkDim + toBlockCoord;
			const vec3 normals[] = { 
				normalize(vec3(+1,  0.6 + (sin( time*0.71)+sin(time))*0.1, 1)), 
				normalize(vec3(-1,  0.6 + (sin(-time*0.71)+sin(time))*0.1, 1)),
				normalize(vec3(+0, -0.6 - (sin(time*0.71)+sin(-time))*0.1, 1))
			};
			
			const vec3 rand_ = vec3(rand(vec2(toBlockAbsoluteCoord.xy)), rand(vec2(toBlockAbsoluteCoord.yz)), rand(vec2(toBlockAbsoluteCoord.xz)));
			for(int index = 0; index < 3; index++) {
				const vec3 normal = normals[index];
				const vec3 dir1 = normal;
				const vec3 dir2 = normalize(cross(vec3(0, 1, 0), dir1));
				const vec3 dir3 = cross(dir1, dir2);
				
				const vec3 center = vec3(toBlockCoord) + vec3(0.5, 0, 0.5) + vec3(1, 0, 1) * (rand_ - 0.5) * 0.3;
				
				const float t = intersectPlane(ray, center, normal);
				if(t >= -0.001) {
					const vec3 intersectionCoord = at(ray, t);
					
					const int newBias = dot(ray.dir, normal) > 0 ? -1 : 0;
					const bool isBias = (dot(ray.orig-intersectionCoord, ray.orig-intersectionCoord) > 0.0001 || bias <= newBias);
					
					const vec3 cubeCoordF = toCubeCoord;
					if(isBias && intersectionCoord == clamp(intersectionCoord, cubeCoordF / cubesInBlockDim, (cubeCoordF+1) / cubesInBlockDim)
							&& shouldWriteInterrsection(intersectionCoord)) {												
						const vec3 localInters = intersectionCoord - center;
						
						const vec2 uv = vec2(dot(localInters, dir2), dot(localInters, dir3)) * mix(vec2(1.0/0.7, 2.1), vec2(1.2,1.5), rand_.xy) + vec2(0.5, 0);
						const bool alpha = uv.y == clamp(uv.y, 0, 1) && (sampleAtlas(alphaAt(tBlockId), mod(uv, 1)).x > 0.5);
						
						if(alpha) bc = BlocksIntersection(intersectionCoord, tBlockId, index, newBias);							
					}
				}
			}
			
			tBlockId = 0;
		}
	}
	if(cubes.w) {
		const LiquidCube tLiquid = liquidAtCube(
			toCubeChunkIndex, 
			cubeLocalToChunk(toCubeCoord)
		);
		
		const ivec3 cubeCoord = toCubeCoord;
		
		const uint liquidLevel = level(tLiquid);
		const uint liquidId = id(tLiquid);
		
		const float levelY = (cubeCoord.y + max((liquidLevel+1) / 16, 1) / 16.0) / cubesInBlockDim;
		const float yLevelDiff = levelY - ray.orig.y;
		
		if(liquidLevel == 255u || (any(intersectionSide.xz) && yLevelDiff >= 0) || (intersectionSide.y && yLevelDiff >= 0))
			tLiquidId = liquidId;
		
		{
			const float yLevelDist = yLevelDiff * (dirPositive.y ? 1 : -1);
			
			if(intersectionSide == bvec3(false) && bias <= 0 && yLevelDiff > 0) { 
				//bug: if camera position is between two liquid cubes' boundaries, it will be rendered incorrectly 
				return BlocksIntersection(ray.orig, liquidId, 1u, 0);
			}
			else if(liquidLevel == 255u) /*skip*/;
			else if(yLevelDist >= 0) {
				const ivec3 intersectionSide = ivec3(0, 1, 0);
				
				vec3 intersectionCoord;
				intersectionCoord.xz = at(ray, yLevelDist / abs(ray.dir.y)).xz;
				intersectionCoord.y = levelY;
				
				const int liquidBias = dirSign.y > 0 ? -2 : 1;
				const bool isBias = (ray.orig.y != levelY || bias <= liquidBias);
				
				const vec3 cubeCoordF = cubeCoord;
				if(isBias && intersectionCoord == clamp(intersectionCoord, cubeCoordF / cubesInBlockDim, (cubeCoordF+1) / cubesInBlockDim)
					&& shouldWriteInterrsection(intersectionCoord)) {
					if(intersectionOnly) bc = BlocksIntersection(intersectionCoord, liquidId, 2u, liquidBias);
				}
			}
		}
	}
	
	#undef shouldWriteInterrsection
	
	
	const uint/*uint16_t[4]*/ blocks[2] = { fLiquidId | (fBlockId << 16), tBlockId | (tLiquidId << 16)  };
	const ivec3 blocksCoord[2] = { cubeBlock(fromCubeCoord), cubeBlock(toCubeCoord) };
	
	uint lowestIntersectionId = 0;
	int lowestBias;
	
	for(int surface = maxBias-1; surface >= minBias; surface--) {
		const uint blockIndex = surface - minBias;
		const uint blockId = (blocks[blockIndex/2] >> (16 * (blockIndex%2))) & 0xffffu;
		const vec3 blockCoord = blocksCoord[int(surface >= 0)];
		
		if(blockId == 0) continue; 
		if(intersectionSide == bvec3(false) || !alphaTest(ray.orig, vec3(intersectionSide), dirSign, blockCoord, blockId)) continue;
		if(blockId == lowestIntersectionId && (isGlass(blockId) || blockId == water || blockId == diamondBlock)) { lowestIntersectionId = 0; continue; }
		if(lowestIntersectionId != 0 && lowestIntersectionId != water && blockId == water) continue; //ignore water backside
		
		if(surface < bias) continue;
		lowestIntersectionId = blockId;
		lowestBias = surface;
	}
	
	if(lowestIntersectionId != 0) {
		const uint blockIndex = lowestBias - minBias;
		const uint blockId = (blocks[blockIndex/2] >> (16 * (blockIndex%2))) & 0xffffu;
		const vec3 blockCoord = blocksCoord[int(lowestBias >= 0)];
		
		bc = BlocksIntersection(ray.orig, blockId, 0u, lowestBias);
	}
	
	return bc;
	#undef dirSign
}

void findIntersections(const int iteration, const int lowestNotUpdated) { 	
	Ray ray;
	vec3 stepLength;
	bvec3 dirPositive;
	bvec3 dirNegative;
	#define dirSign calcDirSign(ray.dir)
	
	ivec3 relativeToChunk;
	int prevChunkIndex;
	int curChunkIndex;
	vec3 curCoord;
	int bias;
	
	uint dataAndId;
	Cubes fromBlock;
	bool isIntersection;
	
	int paramsPos = -1;
					
	while(true) {
		//save current Params and load next
		if(isIntersection || (paramsPos == -1) || any(greaterThan((relativeToChunk - mix(vec3(0), vec3(renderDiameter-1), dirPositive)) * dirSign, ivec3(0)))) {			
			if(paramsPos == -1) {
				paramsPos = curParamsPos();
			}
			else {
				const Params p = readStartParams(paramsPos);
				
				const vec3 startP = playerRelativePosition - vec2(playerWidth/2.0, 0           ).xyx;
				const vec3 endP   = playerRelativePosition + vec2(playerWidth/2.0, playerHeight).xyx;
				const Intersection playerI = intersectCube(p.ray, p.bias, startP, endP, vec3(1,0,0), vec3(0,1,0));
				const bool isPlayer = (drawPlayer || (iteration != 0 && playerI.bias >= 0)) && playerI.is;
				
				bool playerFrist;
				if(isPlayer && isIntersection) {
					const float blocksTsq = dot(curCoord - p.ray.orig, curCoord - p.ray.orig);
					const float playerTsq = dot(playerI.at - p.ray.orig, playerI.at - p.ray.orig);
					playerFrist = blocksTsq > playerTsq;
				}
				else playerFrist = isPlayer;
				
				
				if(playerFrist) {
					const uint data = uint(playerI.side);
					writeEndParams(Params(
						Ray(playerI.at, p.ray.dir), distance(p.ray.orig, playerI.at), playerI.bias, 1 | (1u << 15), data, 
						p.rayId, p.rayType, p.parent, p.last), paramsPos
					);
				}
				else {
					writeEndParams(Params(
						Ray(curCoord, p.ray.dir), distance(p.ray.orig, curCoord), bias, dataAndId & 0xffu, dataAndId >> 16, 
						p.rayId, p.rayType, p.parent, p.last), paramsPos
					);
				}
				
				paramsPos--;
			}
			if(paramsPos < lowestNotUpdated) return;
			
						
			const Params p = readStartParams(paramsPos);
			
			ray = p.ray;
			stepLength = 1.0 / ray.dir;
			dirPositive = isDirPositive(ray.dir);
			dirNegative = isDirNegative(ray.dir);
			
			relativeToChunk = blockChunk(ivec3(floor(ray.orig)));
			curChunkIndex = chunkAt(relativeToChunk);
			curCoord = ray.orig;
			bias = p.bias;
			
			//calculate first intersection
			const ivec3 startBlockCoord = ivec3(floor(curCoord)) - ivec3(and3b(equal(curCoord, floor(curCoord)), dirPositive));
			const int startBlockChunk = chunkAt(blockChunk(startBlockCoord));
			fromBlock = cubesAtBlock(
				startBlockChunk,
				blockLocalToChunk(startBlockCoord)
			);
			prevChunkIndex = startBlockChunk;
			
			dataAndId = 0;
			isIntersection = false;
		}
		
		const vec3 relativeToBlock = vec3(relativeToChunk * blocksInChunkDim);
		//calculate next chunk
		const vec3 relativeCoord = curCoord - relativeToBlock;
		const vec3 farBordersDiff = mix(0 - relativeCoord, blocksInChunkDim - relativeCoord, dirPositive);
		const vec3 farBordersLen = farBordersDiff * stepLength;
		
		const float farBordersMinLen = min(min(farBordersLen.x, farBordersLen.y), farBordersLen.z); //minimum length for any ray axis to get outside of chunk bounds
		const bvec3 firstOut = equal(farBordersLen, vec3(farBordersMinLen));
		const ivec3 outNeighbourDir = ivec3(firstOut) * dirSign;
		
		const int nextChunkIndex = chunkAt(relativeToChunk + outNeighbourDir);
		const ivec3 nextRelativeToChunk = relativeToChunk + outNeighbourDir;
		
		//calculate first intersection with current chunk
		const uint bounds = chunkBounds(curChunkIndex);
		const vec3 startBorder = vec3(start(bounds));
		const vec3 endBorder   = vec3(onePastEnd(bounds));
		const bool empty = emptyBoundsF(startBorder, endBorder);
		
		const vec3 nearBoundaries = mix(startBorder, endBorder, dirNegative);
		const vec3 farBoundaries  = mix(startBorder, endBorder, dirPositive);
		
		const vec3 nearBoundsCoords = nearBoundaries + relativeToBlock;
		const vec3 farBoundsCoords  = farBoundaries  + relativeToBlock;
		
		const bool pastNearBounds = all(greaterThanEqual((curCoord - nearBoundsCoords)*dirSign, vec3(0)));
		const vec3  nearBoundsLen = (nearBoundsCoords - ray.orig) * stepLength;
		const float nearBoundsMinLen = max(max(nearBoundsLen.x, nearBoundsLen.y), nearBoundsLen.z); //minimum length for all ray axis to get inside of chunk bounds
		const bvec3 nearBoundCandMin = equal(nearBoundsLen, vec3(nearBoundsMinLen));
		
		const vec3 candCoord = pastNearBounds ? curCoord : mix(at(ray, nearBoundsMinLen), nearBoundsCoords, nearBoundCandMin);
		const bvec3 inChunkBounds = lessThanEqual((candCoord - farBoundsCoords) * dirSign, vec3(0));
		
		if(!all(inChunkBounds) || empty) {
			prevChunkIndex = curChunkIndex;
			curChunkIndex = nextChunkIndex;
			relativeToChunk = nextRelativeToChunk;
			continue;
		}
		
		
		const vec3 candFromBlockCoord = floor(candCoord) - vec3(and3b(equal(candCoord, floor(candCoord)), dirPositive));
		const vec3 curFromBlockCoord  = floor(curCoord ) - vec3(and3b(equal(curCoord , floor(curCoord)) , dirPositive));
		
		if(blockChunk(ivec3(candFromBlockCoord)) != blockChunk(ivec3(curFromBlockCoord))) prevChunkIndex = curChunkIndex;
		if(candFromBlockCoord != curFromBlockCoord) fromBlock = cubesEmpty();
		if(candCoord != curCoord) bias = minBias;
		curCoord = candCoord;
		
		//step through blocks/cubes of the current chunk
		while(true) {
			const bvec3 intersectionSide = equal(curCoord*cubesInBlockDim, floor(curCoord*cubesInBlockDim));
					
			const ivec3 fromCubeCoord = ivec3(floor(curCoord * cubesInBlockDim)) - ivec3(and3b(dirPositive, intersectionSide));
			const ivec3 toCubeCoord   = ivec3(floor(curCoord * cubesInBlockDim)) - ivec3(and3b(dirNegative, intersectionSide));
			
			const int fromCubeChunkIndex = cubeChunk(fromCubeCoord) == relativeToChunk ? curChunkIndex : prevChunkIndex;
			const int toCubeChunkIndex   = cubeChunk(toCubeCoord  ) == relativeToChunk ? curChunkIndex : nextChunkIndex;
			
			Cubes toBlock;
			
			/*const bvec3 blockBounds = equal(curCoord, floor(curCoord));
			const bool atBlockBounds = any(blockBounds);
			if(!atBlockBounds) toBlock = fromBlock;
			else */
			toBlock = cubesAtBlock(
				toCubeChunkIndex,
				blockLocalToChunk(cubeBlock(toCubeCoord))
			);

			if(!isEmpty(fromBlock) || !isEmpty(toBlock)) {
				const bvec4 cubes = bvec4(
					blockCubeAt (fromBlock, cubeLocalToBlock(fromCubeCoord)),
					liquidCubeAt(fromBlock, cubeLocalToBlock(fromCubeCoord)),
					blockCubeAt (toBlock  , cubeLocalToBlock(toCubeCoord  )),
					liquidCubeAt(toBlock  , cubeLocalToBlock(toCubeCoord  ))
				);
				
				if(any(cubes)) {					
					const BlocksIntersection intersection = blocksIntersection(
						Ray(curCoord, ray.dir), intersectionSide, bias,
						fromCubeCoord, toCubeCoord, cubes, 
						fromCubeChunkIndex, toCubeChunkIndex
					);
					
					bias = intersection.bias;
					
					if(bias != maxBias) {
						isIntersection = true;
						curCoord = intersection.coord;
						dataAndId = (intersection.id & 0xffffu) | (intersection.data << 16);
						break; //all the way to the start
					}
				}		
			}
			
			//calculate next intersection
			const bool hnn = hasNoNeighbours(toBlock) || neighboursFullSameLiquid(toBlock);
			const float skipDistance = hnn ? 1.0 : (isEmpty(toBlock) || fullSameLiquid(toBlock) ? 1.0 : cubesInBlockDim);
			const vec3 candCoords = floor(curCoord * skipDistance * dirSign + (hnn ? 2 : 1)) / skipDistance * dirSign;
	
			const vec3 nextLenghts = (candCoords - ray.orig) * stepLength;
			
			const float nextMinLen = min(min(nextLenghts.x, nextLenghts.y), nextLenghts.z);
			const bvec3 nextMinAxis = equal(nextLenghts, vec3(nextMinLen));
			
			curCoord = mix(
				max(curCoord*dirSign, at(ray, nextMinLen)*dirSign)*dirSign, 
				candCoords, 
				nextMinAxis
			);
			bias = minBias;
			fromBlock = toBlock;
			
			
			const bool inChunkBounds = all(lessThanEqual((curCoord - farBoundsCoords) * dirSign, vec3(0)));
			if(!inChunkBounds) {
				prevChunkIndex = curChunkIndex;
				curChunkIndex = nextChunkIndex;
				relativeToChunk = nextRelativeToChunk;
				break;
			}
		}
	}
	#undef dirSign
}


// intersection resolving //
vec3 background(const vec3 dir) {
	const float t = 0.5 * (dir.y + 1.0);
	const vec3 res = mix(vec3(0.6, 0.7, 0.9), vec3(0.43, 0.7, 1.15), t);
	return pow(res*1.7, vec3(2.2));
}

vec3 fade(const vec3 color, const float depth) {
	const float bw = 0.5 + dot(color, vec3(0.299, 0.587, 0.114)) * 0.5;
	return mix(vec3(bw), color, inversesqrt(depth / 500 + 1));
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
	
	if(isGlass(surfaceId) || surfaceId == 15 || surfaceId == diamondBlock) {
		const bool glass = isGlass(surfaceId);
		
		const float fresnel = unpackHalf2x16(current.data).x;
		const bool backside = bool(current.data >> 16);
		const bool both = childrenCount == 2;
		
		for(int i = 0; i < childrenCount; i++) {
			const Result inner = readResult(currentIndex+1 + i);
			
			if(isShadow) {
				if(inner.rayType == rayTypeShadowSky) result.rayType = rayTypeShadowSky;
				else continue;
			}			
			if(isLighting) {
				if(inner.rayType == rayTypeLightingEmitter) result.rayType = rayTypeLightingEmitter;
				else continue;
			}
			
			if(inner.rayId != 0 && inner.rayId != 1) discard;
			const bool reflect = inner.rayId == 1;
			
			const vec3 innerCol = inner.color * (both ? (reflect ? fresnel : 1 - fresnel) : 1.0) * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0);
			const vec3 waterInnerCol = ( backside ^^ reflect ? innerCol : mix(current.color, innerCol, exp(-inner.depth)) );
			
			result.color += glass || surfaceId == diamondBlock ? (current.color * innerCol) : ( reflect ? mix(current.color, waterInnerCol, 0.97) : waterInnerCol );
		}
	}
	else if(surfaceId == 26) {
		for(int i = 0; i < childrenCount; i++) {
			const Result inner = readResult(currentIndex+1 + i);
			if(isShadow) {
				if(inner.rayType == rayTypeShadowSky) result.rayType = rayTypeShadowSky;
				else continue;
			}
			if(isLighting) {
				if(inner.rayType == rayTypeLightingEmitter) result.rayType = rayTypeLightingEmitter;
				else continue;
			}
			result.color = mix(current.color, current.color * inner.color * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0), 0.8);
		}
	}
	else {
		int i = 0;
				
		if(current.rayType == rayTypeStandard) {
			vec3 emitterColor = vec3(-1);
			if(i < childrenCount) { //emitter
				const Result inner = readResult(currentIndex+1 + i);
				
				if(inner.rayType == rayTypeLightingBlock || inner.rayType == rayTypeLightingEmitter) {
					result.color += inner.rayType == rayTypeLightingEmitter ? (inner.color / (1 + inner.depth*inner.depth)) : vec3(0);
					i++;
				}
			}
			
			if(i < childrenCount) { //shadow
				const Result inner = readResult(currentIndex+1 + i);
				
				if(inner.rayType == rayTypeShadowBlock || inner.rayType == rayTypeShadowSky) {
					const vec3 shadowTint = inner.rayType == rayTypeShadowSky ? inner.color : vec3(0.0);
					result.color += current.color * shadowTint;
					i++;
				}
			}
		}
		
		if((surfaceId == 16 || surfaceId == 5) && i < childrenCount) {
			const Result inner = readResult(currentIndex+1 + i);
			
			if(inner.rayId == 1) {
				if(inner.rayType == rayTypeShadowSky) {
					result.rayType = rayTypeShadowSky;
					result.color += current.color * inner.color;
				}
				else if(inner.rayType == rayTypeLightingEmitter) {
					result.rayType = rayTypeLightingEmitter;
					result.color += current.color * inner.color * (isLighting ? 1.0 / (1 + inner.depth*inner.depth) : 1.0);
				}
				else if(inner.rayType == rayTypeLightingBlock || inner.rayType == rayTypeShadowBlock) /*do nothing*/;
				else result.color += current.color * inner.color;
				
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

const int shadowOffsetsCount = 4;
const float shadowOffsets[] = { -1, 0, 1, 0 };

const int shadowSubdiv = 16;
const float shadowSmoothness = 32;
const int shadowsInChunkDim = blocksInChunkDim * shadowSubdiv;	

void pushShadowEndEmmitter(
	inout bool pushed,
	const Params params, const int curFrame, const int intersectionChunkIndex,
	const ivec3 relativeToChunk, const vec3 normalDir, const vec3 position
) {
	const uvec3 shadowInChunkCoord = uvec3(floor(mod(position*shadowSubdiv, vec3(shadowsInChunkDim))));
	const uint shadowInChunkIndex = 
		  shadowInChunkCoord.x
		+ shadowInChunkCoord.y * uint(shadowsInChunkDim)
		+ shadowInChunkCoord.z * uint(shadowsInChunkDim) * uint(shadowsInChunkDim);
	const int rShadowInChunkIndex = mapInteger(int(shadowInChunkIndex)) / 32;
	const int dirIndex = int(uint(rShadowInChunkIndex) % 42);
	
	if(canPushParams()) { //shadow
		const vec3 offset_ = vec3(
			shadowOffsets[ uint(rShadowInChunkIndex) % shadowOffsetsCount ],
			shadowOffsets[ uint(rShadowInChunkIndex/8) % shadowOffsetsCount ],
			shadowOffsets[ uint(rShadowInChunkIndex/32) % shadowOffsetsCount ]
		);
		const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness;
		const vec4 q = vec4(normalize(vec3(-1-sin(time/4)/10,3,2)), cos(time/4)/5);
		const vec3 v = normalize(vec3(-1.5, 4, 2) + offset);
		const vec3 temp = cross(q.xyz, v) + q.w * v;
		const vec3 rotated = v + 2.0*cross(q.xyz, temp);
		
		const vec3 newDir = normalize(rotated);
		const int newBias = params.bias * ((dot(params.ray.dir, normalDir) > 0) == (dot(newDir, normalDir) > 0) ? 1 : -1);
		
		pushStartParams(Params(Ray(position, newDir), 0.0, newBias, 0u, 0u, 0u, rayTypeShadowBlock, curFrame, !pushed));
		pushed = true;
	}
	
	if(canPushParams()) { //ray to emitter
		const NE ne = neFromChunk(intersectionChunkIndex, dirIndex);
		const bool emitters = ne.capacity != 0u && (ne.capacity == 1u ? dirIndex < 30 : true);
		
		if(emitters) {
			// calculate random vector that hits the sphere inside emitter block //
			
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
			
			const int newBias = params.bias * ((dot(params.ray.dir, normalDir) > 0) == (dot(newDir, normalDir) > 0) ? 1 : -1);
			
			if((abs(dot(newDir, newDir) - 1) < 0.01)) {
				pushStartParams(Params(Ray(position, newDir), 0.0, newBias, 0u, 0u, 0u, rayTypeLightingBlock, curFrame, !pushed));
				pushed = true;
			}
		}
	}
}

RayResult resolveIntersection(const int iteration) {
	bool pushed = false;
	
	const int curFrame = putResultPos;
	
	const Params p = popEndParams();
	//if(!canPushResult()) ???; //this check is not needed because sizeof(Result) < sizeof(Params), so we would have space for at least 1 Result
	const Ray ray = p.ray;
	const ivec3 dirSign = calcDirSign(ray.dir);
	
	const uint rayId = p.rayId;
	const uint type = p.rayType;
	const int bias = p.bias;
	const int parent = p.parent;
	const bool last = p.last;
	const float t = p.distance;
	const uint surfaceId = p.id;
	const uint data = p.data;
	
	const bool shadow = type == rayTypeShadowBlock || type == rayTypeShadowSky || type == rayTypeLightingBlock || type == rayTypeLightingEmitter;
	
	if(surfaceId != 0) {
		const bool isPlayer = surfaceId == (1u | (1u << 15));
		const uint blockId = surfaceId;
		const bvec3 cubeIntersectionsSide = equal(p.ray.orig*cubesInBlockDim, floor(p.ray.orig*cubesInBlockDim));
		
		const ivec3 cubeCoord = ivec3(floor(ray.orig * cubesInBlockDim)) - ivec3(
			bias >= 0 ? and3b(greaterThanEqual(ray.dir, vec3(0)), cubeIntersectionsSide)
			          : and3b(lessThan        (ray.dir, vec3(0)), cubeIntersectionsSide)
		);
		const ivec3 blockCoord = cubeBlock(cubeCoord);
		const ivec3 chunkPosition = cubeChunk(cubeCoord);
		const int cubeChunkIndex = chunkAt(chunkPosition);
		const vec3 coord = ray.orig;
		const vec3 localSpaceCoord = coord - vec3(blockCoord);
		
		bvec3 intersectionSide_ = cubeIntersectionsSide;
		vec3 normalDir = vec3(intersectionSide_);

		if(blockId == 15) {
			intersectionSide_ = (bool(data == 1) ? bvec3(false) : (data == 2 ? bvec3(false, true, false) : intersectionSide_));
			normalDir = vec3(intersectionSide_);
		}
		else if(isPlayer) {
			const vec3 normals[] = { vec3(1,0,0), vec3(0,1,0), vec3(0,0,1) };
			normalDir = normals[data];
			intersectionSide_ = notEqual(normalDir, vec3(0));
		}
		
		vec2 uvAbs = blockUv(coord, ivec3(intersectionSide_), dirSign);
		vec2 uv = mod(uvAbs, 1);
		
		if(blockId == 16) {
			const ivec3 blockAbsolutePos = chunksOffset * blocksInChunkDim + blockCoord;
			const vec3 normals[] = { 
				normalize(vec3(+1,  0.6 + (sin( time*0.71)+sin(time))*0.1, 1)), 
				normalize(vec3(-1,  0.6 + (sin(-time*0.71)+sin(time))*0.1, 1)),
				normalize(vec3(+0, -0.6 - (sin(time*0.71)+sin(-time))*0.1, 1))
			};
			
			const int index = int(data);
			const vec3 rand_ = vec3(rand(vec2(blockAbsolutePos.xy)), rand(vec2(blockAbsolutePos.yz)), rand(vec2(blockAbsolutePos.xz)));
			
			const vec3 normal = normals[index];
			const vec3 dir1 = normal;
			const vec3 dir2 = normalize(cross(vec3(0, 1, 0), dir1));
			const vec3 dir3 = cross(dir1, dir2);
			
			const vec3 center = vec3(blockCoord) + vec3(0.5, 0, 0.5) + vec3(1, 0, 1) * (rand_ - 0.5) * 0.3;			
			const vec3 localInters = ray.orig - center;
			
			uvAbs = vec2(dot(localInters, dir2), dot(localInters, dir3)) * mix(vec2(1.0/0.7, 2.1), vec2(1.2,1.5), rand_.xy) + vec2(0.5, 0);

			normalDir = normal;
			uv = mod(uvAbs, 1);
		}
		
		const bvec3 intersectionSide = intersectionSide_;
		
		const ivec3 side = mix(ivec3(0), (bias >= 0 ? -1 : 1) * dirSign, intersectionSide);
		const ivec3 normal = mix(ivec3(0), -dirSign, intersectionSide);
		const bool backside = bias < 0;
		
		const vec2 atlasOffset = atlasAt(blockId, side);
		
		
		float light;
		float ambient;
		if(!shadow && !isPlayer) {				
			const ivec3 otherAxis1 = intersectionSide.x ? ivec3(0,1,0) : ivec3(1,0,0);
			const ivec3 otherAxis2 = intersectionSide.z ? ivec3(0,1,0) : ivec3(0,0,1);
			const ivec3 vertexCoord = ivec3(floor(coord * cubesInBlockDim)) - chunkPosition * cubesInChunkDim;//cubeLocalToChunk(cubeCoord);
			
			ambient = 0;
			if(!backside) {
			  #if 0
				ambient = aoForChunkVertexDir(cubeChunkIndex, chunkPosition, vertexCoord, normal);
			  #else
				for(int i = 0 ; i < 4; i++) {
					const ivec2 offset_ = ivec2(i%2, i/2);
					const ivec3 offset = offset_.x * otherAxis1 + offset_.y * otherAxis2;
					const ivec3 curVertexCoord = vertexCoord + offset;
										
					const float vertexAO = aoForChunkVertexDir(cubeChunkIndex, chunkPosition, curVertexCoord, normal);
					
					const float diffX = abs(offset_.x - mod(dot(localSpaceCoord, otherAxis1)*2, 1));
					const float diffY = abs(offset_.y - mod(dot(localSpaceCoord, otherAxis2)*2, 1));
					ambient += mix(mix(vertexAO, 0, diffY), 0, diffX);
				}
			  #endif
			}
			else ambient = 0.5;
			
			#if 0
			ambient -= 0.7;
			ambient *= 3;
			#endif
			
			light = max(
				bias >= 0 ? lightingAtCube(cubeChunkIndex, chunkPosition, vertexCoord - ivec3(and3b(intersectionSide, isDirNegative(ray.dir)))) : 0.0,
				bias <= 0 ? lightingAtCube(cubeChunkIndex, chunkPosition, vertexCoord - ivec3(and3b(intersectionSide, isDirPositive(ray.dir)))) : 0.0
			);
		}
		else {
			light = 1;
			ambient = 1;
		}
		
		if(isGlass(surfaceId) || blockId == 15 || blockId == diamondBlock) {
			const bool glass = isGlass(surfaceId);

			const float offsetMag = clamp(t * 50 - 0.01, 0, 1);
			const vec3 glassOffset = vec3( 
				sin(localSpaceCoord.x*9)/2, 
				sin(localSpaceCoord.z*9)/8, 
				sin(localSpaceCoord.y*6 + localSpaceCoord.x*4)/8
			) / 100;
			const vec3 waterOffset = (texture(noise, uvAbs / 5  + (texture(noise, uvAbs/30 + time/60).xy-0.5) * 0.8 ).xyz - 0.5) * 0.2;
			const vec3 offset_ = (glass ? glassOffset : waterOffset);
			const vec3 offset = offset_ * offsetMag;
			
			const vec3 newBlockCoord = localSpaceCoord + offset * vec3(not(intersectionSide));
			
			const vec3 coordInCube = mod(coord, 1.0 / cubesInBlockDim);
			const vec3 offsetedCoordInCube = 1.0 / cubesInBlockDim - abs(1.0 / cubesInBlockDim - mod(coordInCube + offset, 2.0 / cubesInBlockDim));/*
				zigzags between 0 and 1.0 / cubesInBlockDim,
				used to get nicer offsetedCoord when coord is close to cube's edges
			*/
			const vec3 offsetedCoord = mix(
				coord + (offsetedCoordInCube - coordInCube),
				coord, 
				intersectionSide
			);

			const vec2 newUV = mod(blockUv(newBlockCoord, ivec3(intersectionSide), dirSign), 1);
			const vec3 color = fade(sampleAtlas(atlasOffset, newUV) * mix(0.5, 1.0, ambient), t);
			
			vec3 incoming = any(intersectionSide) ? ray.dir : normalize(ray.dir * (1.01 + offset_));
			if(any(isnan(incoming))) incoming = ray.dir;
			
			const vec3 normalDir = any(intersectionSide) ? normalize(vec3(normal) + offset * vec3(not(intersectionSide))) : -incoming;
			const float ior = glass ? 1.00 : (blockId == diamondBlock ? 1.3 : 1.05);
			
			//https://developer.blender.org/diffusion/B/browse/master/intern/cycles/kernel/closure/bsdf_util.h$13
			//fresnel_dielectric()
			vec3 refracted;
			float fresnel;
			{
				const vec3 N = (backside ? 1 : -1) * normalDir;
				const vec3 I = incoming;
				const float eta = ior;
				
				float cos = dot(N, I), neta;
				vec3 Nn;
				
				// check which side of the surface we are on
				if (cos > 0) {
					// we are on the outside of the surface, going in
					neta = 1 / eta;
					Nn = N;
				}
				else {
					// we are inside the surface
					cos = -cos;
					neta = eta;
					Nn = -N;
				}
				
				float arg = 1 - (neta * neta * (1 - (cos * cos)));
				if (arg < 0) {
					refracted = vec3(0);
					fresnel = 1;  // total internal reflection
				}
				else {
					float dnp = max(sqrt(arg), 1e-7f);
					float nK = (neta * cos) - dnp;
					refracted = -(neta * I) + (nK * Nn);

					// compute Fresnel terms
					float cosTheta1 = cos;  // N.R
					float cosTheta2 = -dot(Nn, refracted);
					float pPara = (cosTheta1 - eta * cosTheta2) / (cosTheta1 + eta * cosTheta2);
					float pPerp = (eta * cosTheta1 - cosTheta2) / (eta * cosTheta1 + cosTheta2);
					fresnel = 0.5f * (pPara * pPara + pPerp * pPerp);
					refracted = -refracted;
				}
			}
			
			const bool isRefracted = refracted != vec3(0);
			const bool isReflected = any(intersectionSide) ? true || (isRefracted ? !backside && iteration < 3 : true) : false;
			
			const uint data = (packHalf2x16(vec2(fresnel, 0.0)) & 0xffffu) | (uint(backside) << 16);
			
			pushResult(Result( color, t, data, blockId, rayId, type, parent, last ));			
			
			if(isRefracted && canPushParams()) {
				const vec3 newDir = refracted;
				const int newBias = bias + 1;
				
				pushStartParams(Params(Ray(offsetedCoord, newDir), 0.0, newBias, 0u, 0u, 0u, type, curFrame, !pushed));
				pushed = true;
			}	
			
			if(isReflected && canPushParams()) {
				const vec3 newDir = reflect(incoming, normalDir);
				const int newBias = -bias;
				
				pushStartParams(Params(Ray(offsetedCoord, newDir), 0.0, newBias, 0u, 0u, 1u, type, curFrame, !pushed));
				pushed = true;
			}
		}
		else if(blockId == 26) {
			const vec3 offset = normalize(texture(noise, uv/5).xyz - 0.5) * 0.01;
			
			const vec3 newBlockCoord = localSpaceCoord - offset;
			const vec2 newUv = clamp(blockUv(newBlockCoord, ivec3(intersectionSide), dirSign), 0.0001, 0.9999);
			
			const vec3 color_ = sampleAtlas(atlasOffset, newUv) * light * mix(0.3, 1.0, ambient);
			const vec3 color = fade(color_, t);
			
			
			pushResult(Result( color, t, 0u, blockId, rayId, type, parent, last));
			if(canPushParams()) {
				const vec3 reflected_ = reflect( ray.dir, normalize( normal + offset ) );
				const vec3 reflected  = abs(reflected_) * normal + reflected_ * (1 - abs(normal));
			
				pushStartParams(Params( Ray(coord, reflected), 0.0, -bias, 0u, 0u, 0u, type, curFrame, !pushed));
				pushed = true;
			}
		}
		else {
			const bool isBlockEmitter = blockId == 13 || blockId == 14;
			float brightness;
			if(blockId == 13) brightness = 2.5;
			else if(blockId == 14) brightness = 2.5 + (sin(time*2)+sin(time*17.3)+sin(time*7.51))*0.2;
			else brightness = 1;
			
			const ivec3 relativeToChunk = chunkPosition;
			const float AO = blockId == 16 ? 1.0 : mix(0.3, 1.0, ambient);
			const vec3 color_ = isPlayer ? vec3(1) : (sampleAtlas(atlasOffset, uv) * (isBlockEmitter ? brightness : light * AO));
			const vec3 color = fade(color_, t);
			

			pushResult(Result( color, t, 0u, blockId, rayId, (isBlockEmitter && type == rayTypeLightingBlock) ? rayTypeLightingEmitter : type, parent, last));
			
			if((blockId == 5 || blockId == 16) && shadow && canPushParams()) {
				pushStartParams(Params( Ray(coord, ray.dir), 0.0, bias+1, 0u, 0u, 1u, type, curFrame, !pushed));
				pushed = true;
			}
			if(!shadow && !isBlockEmitter) {
				vec3 position;
				if(blockId == 16) {
					const ivec3 blockAbsolutePos = chunksOffset * blocksInChunkDim + blockCoord;
					const vec3 rand_ = vec3(rand(vec2(blockAbsolutePos.xy)), rand(vec2(blockAbsolutePos.yz)), rand(vec2(blockAbsolutePos.xz)));
					const vec3 dir1 = normalDir;
					const vec3 dir2 = normalize(cross(vec3(0, 1, 0), dir1));
					const vec3 dir3 = cross(dir1, dir2);
					
					const vec2 diffUV = (uvAbs - (floor(uvAbs * shadowSubdiv) + vec2(0.501)) / shadowSubdiv) / mix(vec2(1.0/0.7, 2.1), vec2(1.2,1.5), rand_.xy);
					
					position = coord - (dir2 * diffUV.x + dir3 * diffUV.y);
				}
				else if(blockId == (1u | (1u << 15))) {			
					position = mix((floor(ray.orig * shadowSubdiv) + vec3(0.501, 0.501, 0.501)) / shadowSubdiv, ray.orig, intersectionSide);
				}
				else position = ( floor(coord * shadowSubdiv) + vec3(not(intersectionSide)) * vec3(0.501, 0.501, 0.501) ) / shadowSubdiv;

				pushShadowEndEmmitter(
					pushed,
					p, curFrame, cubeChunkIndex,
					relativeToChunk, normalDir, position
				);
			}
		}
	}
	else { //sky
		pushResult(Result(shadow ? vec3(1) : background(ray.dir), far, bias, 0u, rayId, type == rayTypeShadowBlock ? rayTypeShadowSky : type, parent, last));
	}
	
	return RayResult(pushed);
}

// tracing //
vec3 trace(const Ray startRay) {  
  #if TEST_FIND && READ_TRACE
	setCurParamsPos(int(getMaxGetPos()) - 1);
	int iteration = 0;
	int lowestNotUpdated = 0;

	findIntersections(iteration, lowestNotUpdated);
	
	//const int testResult = 0;
	//if(testResult < getMaxGetPos()) {
	//	setCurParamsPos(testResult);
	//	const RayResult result = resolveIntersection(iteration);
	//	return readResult(0).color;
	//}
	
	return vec3(0.0, 1.8, 1.9);
  #else
	if(canPushParams()) {
		pushStartParams(Params(startRay, 0.0, 0, 0u, 0u, 0u, rayTypeStandard, curResultPos(), true));
	}
	
	int iteration = 0;
	int lowestNotUpdated = curParamsPos();
	
	while(true) {
		const bool lastRay = !canPopParams();
		if(!lastRay) {
			#if TEST_RESOLVE_COMBINE && READ_TRACE
				//endParams are saved in trace buffer
			#else
				findIntersections(iteration, lowestNotUpdated);
			#endif
			
			#if DEBUG
			if(exit_) return vec3(0);
			#endif
			lowestNotUpdated = curParamsPos();//current params will be popped and replaced (lowest param will be at this index)
			const RayResult result = resolveIntersection(iteration);
			#if DEBUG
			if(exit_) return vec3(0);
			#endif
			iteration++;
			if(result.pushedRays) {
				continue;
			}
		}

		bool stop = true;
		int curFrame = curResultPos();
		for(;curFrame >= 0;) {
			const Result currentResult = readResult(curFrame);
			const bool last = currentResult.last;
			const int currentParent = currentResult.parent;
			if(currentParent < 0) { stop = true; break; }
			
			stop = false;
			if(!last && !lastRay) break;
			combineSteps(currentParent, curFrame);
			curFrame = currentParent;
		}
		setCurResultPos(curFrame);
		if(stop) break;
	}
		
	if(curResultPos() < 0) return vec3(1, 0, 1);
		
	return readResult(0).color;
  #endif
}


coherent buffer Luminance {
	uint data[256];
} luminance;

uniform float minLogLum;
uniform float rangeLogLum;
//ERROR: global const initializers must be constant
/*const*/ float x = (255.0 / rangeLogLum);

//https://bruop.github.io/exposure/
uint colorToBin(const vec3 hdrColor) {
	const float lum = dot(hdrColor, rgbToLum);
	if (lum < 0.0001) return 0;
	return clamp(int((log2(lum) - minLogLum) * x), 0, 255);
}

void main() {
	const vec2 coordOffset = 0.25*vec2(rand(gl_FragCoord.xy), rand(-gl_FragCoord.xy));
    const vec2 coord = (gl_FragCoord.xy + coordOffset - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const Ray ray = Ray(startCoord, rayDir);

	const vec3 col = trace(ray);
	
	const float brightness = dot(col, rgbToLum) - 1.2;
	const float offset = 1.0 / 6;
	const float b = 11;
	const float x = (brightness - offset) * b;
	
    if(x >= 0) brightColor = vec4(col * (log(x+1)*offset + offset), 1.0);
	else brightColor = vec4(col * exp(x) * offset, 1.0);
	
	atomicAdd(luminance.data[colorToBin(col)], 1);
	
	color = vec4(col, 1);
	
	#if DEBUG
	if(anyInvocationARB(exit_)) { color = vec4(exitVec3, 1.0); return; }
	#endif
}