#version 450


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

uniform int startChunkIndex;

uniform sampler2D noise;

uniform vec3 playerRelativePosition;
uniform bool drawPlayer;
uniform float playerWidth;
uniform float playerHeight;

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

restrict readonly buffer ChunksBlocks {
     uint data[][16*16*16];
} chunksBlocks;

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



#define neighboursCount 6
restrict readonly buffer ChunksNeighbours {
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
		return indexAsNeighbourDir(index) == ivec3(0);
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

	
restrict readonly buffer ChunksPoistions {
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


restrict readonly buffer ChunksSkyLighting {
    uint data[];
} skyLighting;

restrict readonly buffer ChunksBlockLighting {
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

  struct NE { ivec3 coord; bool is; };
  NE neFromChunk(const int chunkIndex, const int someindex) { 
	const int neIndex = someindex % neCapacity;
	const int chunkOffset = chunkIndex * 16;
	const int neOffset = neIndex / 2;
	const int neShift = (neIndex % 2) * 16;
	
	const uint first = emitters.data[chunkOffset];
	const uint ne16 = (emitters.data[chunkOffset + 1 + neOffset] >> neShift) & 0xffffu;
	
	const uint ne_index = ne16 | (((first >> (2 + neIndex))&1u)<<16);
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

#if 0
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

#endif

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

bvec3 and3b/*seems that glsl has no && for bvec_*/(const bvec3 a, const bvec3 b) { return bvec3(a.x && b.x, a.y && b.y, a.z && b.z); }

vec2 blockUv(const vec3 localBlockCoord, const vec3 intersectionSide, const vec3 dirSign) {
	const float intersectionUSign = dot(dirSign, intersectionSide);
	
	const vec2 uv = vec2(
		dot(intersectionSide, vec3(localBlockCoord.zx, 1-localBlockCoord.x)),
		dot(intersectionSide, localBlockCoord.yzy)
	);
	
	return clamp(vec2(
		mix(uv.x, 1 - uv.x, (intersectionUSign+1) / 2),
		uv.y
	), vec2(0.00001), vec2(0.99999));
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

vec3 calcDirSign(const vec3 dir) {
	return vec3(greaterThanEqual(dir, vec3(0))) * 2 - 1;/*
		dirSign components must not be 0, because isInters relies on the fact that
		someValue * dirSign * dirSign == someValue
		
		sign(dir) can cause the algorithm to go into an infinite loop.
		This for example happens when trying go compute ray-to-emitter intersection
		when starting position is on the same x, y, or z coordinate as the emitter.
	*/
}

IntersectionInfo isInters(const Ray ray, ivec3 relativeToChunk_, const int /*must be loaded*/ startChunkIndex, const int startBias, const float stopAtLen /*and return no intersection with chunkIndex*/) { 
	const vec3 dir = ray.dir;
	const vec3 dirSign = calcDirSign(dir);
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
				
		if(!any(and3b(equal(curCoord, candCoords), nextMinAxisB))) bias = -1;
		
		curCoord = mix(
			at(ray, nextMinLen), 
			candCoords, 
			nextMinAxisB
		);
		
	}

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
		
		const bool stopInThisChunk = stopAtLen <= minOutLen;
		const vec3 candCoord = pastNearBounds ? curCoord : mix(at(ray, nearBoundsCandMinLen), nearBoundsCandCoords, nearBoundCandMinB);
		const bvec3 inChunkBounds = lessThanEqual((candCoord - relativeTo - farBoundaries) * dirSign, vec3(0,0,0));
		
		if(!all(inChunkBounds) || empty) {
			curChunkIndex = nextNotLoaded ? -1 : nextChunkIndex;
			relativeTo = nextRelativeTo;
		}
		else {
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
				
				const ivec3 relativeToBlockCoordI = ivec3(toBlockCoord - relativeTo);
				
			 /* if(!atBlockBounds) toBlock = fromBlock;
				else */
				if(checkBoundaries(relativeToBlockCoordI)) toBlock = blockAt(
					curChunkIndex,
					relativeToBlockCoordI
				);
				else if(nextNotLoaded) toBlock = blockAir();
				else toBlock = blockAt(
					nextChunkIndex,
					ivec3(toBlockCoord - nextRelativeTo) //and3i(relativeToBlockCoordI + blocksInChunkDim, blocksInChunkDim-1)
				);
				
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
						if(stopAtLen*stopAtLen <= dot(curCoord - ray.orig, curCoord - ray.orig)) break;
						
						const ivec3 intersectionSide = ivec3(cubeBounds);
						
						const uint blocks[] = { fBlockUsedId, tBlockUsedId };
						const vec3 blocksCoord[] = { fromBlockCoord, toBlockCoord };
						
						for(bias = max(bias, -1 + int(blocks[0]==0)); bias < 1 - int(blocks[1]==0); bias ++) {
							const uint blockId = blocks[bias+1];
							const vec3 blockCoord = blocksCoord[bias+1];
							
							if(blockId != 0) { 
								if(!alphaTest(curCoord, vec3(intersectionSide), dirSign, blockCoord, blockId)) continue;
								
								const vec3 localBlockCoord = curCoord - blockCoord;
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
				const vec3 candCoords = floor(curCoord * skipDistance * dirSign + (hnn ? 2 : 1)) / skipDistance * dirSign;
	
				const vec3 nextLenghts = (candCoords - ray.orig) * dirSign * stepLength;
				
				const float nextMinLen = min(min(nextLenghts.x, nextLenghts.y), nextLenghts.z);
				const bvec3 nextMinAxisB = equal(nextLenghts, vec3(nextMinLen));
				
				curCoord = mix(
					max(curCoord*dirSign, at(ray, nextMinLen.x)*dirSign)*dirSign, 
					candCoords, 
					nextMinAxisB
				);
				bias = -1;
				
				const bool inChunkBounds = all(lessThanEqual((curCoord - relativeTo - farBoundaries) * dirSign, vec3(0,0,0)) );
		
				if(!inChunkBounds) {
					if(stopInThisChunk) break;
					curChunkIndex = nextNotLoaded ? -1 : nextChunkIndex;
					relativeTo = nextRelativeTo;
					break;
				}
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
	const vec3 res = (1.0 - t) * vec3(1.0, 1.0, 1.02) + t * vec3(0.5, 0.7, 1.0);
	return pow(res*1.2, vec3(2.2));
}


struct Params {
	Ray ray;
	int chunkIndex;
	int bias;
	uint type;
	int parent;
}; 

struct Result {
	vec3 color;
	float depth;
	Surface surface;
	uint type;
	int parent;
};

int putResultPos = 0;
int putParamsPos = 0;

const int size = 40;
uint stack[size]; 
//add results ->              <- add params
//result | result | 0 | 0 | params | params;
//   putResultPos ^   ^ putParamsPos




int curResultPos() { return putResultPos - 1; }

int paramsPosOffset(const int pos) { return size - (pos+1) * 8; }
int resultPosOffset(const int pos) { return pos * 4; }

void writeParams(const Params it, const int position) {
	const int offset = paramsPosOffset(position);
	
	stack[offset+0] = floatBitsToUint(it.ray.orig.x);
	stack[offset+1] = floatBitsToUint(it.ray.orig.y);
	stack[offset+2] = floatBitsToUint(it.ray.orig.z);
	
	stack[offset+3] = floatBitsToUint(it.ray.dir.x);
	stack[offset+4] = floatBitsToUint(it.ray.dir.y);
	stack[offset+5] = floatBitsToUint(it.ray.dir.z);
	
	stack[offset+6] = uint(it.chunkIndex);
	
	stack[offset+7] = (uint(it.parent) & 0xffffu) | (uint(sign(it.bias)+1) << 16) | (it.type << 18);
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
		int(stack[offset+6]),
		int((stack[offset+7] >> 16) & 3u)-1,
		uint(stack[offset+7] >> 18),
		int(stack[offset+7] << 16) >> 16 //sign extended shift
	);
}

void writeResult(const Result it, const int position) {
	const int offset = resultPosOffset(position);
	
	stack[offset+0] = packHalf2x16(it.color.xy);
	stack[offset+1] = packHalf2x16(vec2(it.color.z, it.depth));
	
	stack[offset+2] = it.surface.data;
	stack[offset+3] = (uint(it.parent) & 0xffffu) | (it.type << 16);
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
		Surface(stack[offset+2]),
		stack[offset+3] >> 16,
		int(stack[offset+3] << 16) >> 16 //sign extended shift
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

Result combineSteps(const Result current, const Result inner) {
	const vec3 curColor = current.color;
	
	const bool curShadow = current.type == 1 || current.type == 2;
	const bool innerShadow = inner.type == 1 || inner.type == 2;
	
	const Surface resultingSurface = innerShadow ? inner.surface : current.surface;
	
	if(innerShadow && !curShadow) {
		const bool lighting = inner.type == 2;
		const uint innerSurface = surfaceType(inner.surface);
		const bool emitter = innerSurface == 13 || innerSurface == 14;
		const vec3 shadowTint = lighting ? vec3(1) : (surfaceType(resultingSurface) == 0 ? inner.color : vec3(0.4));
		return Result(
			curColor * shadowTint + (lighting && emitter ? (inner.color / (0.4 + inner.depth*inner.depth)) : vec3(0)),
			current.depth + inner.depth, 
			current.surface, 
			current.type,
			current.parent
		);
	}
	
	const uint type = surfaceType(current.surface);
	
	if(type == 7) return Result(curColor * inner.color, current.depth + inner.depth, resultingSurface, current.type, current.parent);
	else if(type == 8) return Result(curColor * inner.color, current.depth + inner.depth, resultingSurface, current.type, current.parent);
	else return Result(curColor, current.depth, resultingSurface, current.type, current.parent);
}

vec3 trace(const Ray startRay, const int startChunkIndex) {
	const ivec3 startChunkPosition = chunkPosition(startChunkIndex);
	
	if(!canPushParams()) return vec3(1,0,1);
	pushParams(Params(startRay, startChunkIndex, 0, 0u, curResultPos()));
	
	while(true) {
		if(!canPopParams()) break;
		const int curFrame = putResultPos;
		
		const Params p = popParams();
		//if(!canPushResult()) ???; //this check is not needed because sizeof(Result) < sizeof(Params), so we would have space for at least 1 Result
		const Ray ray = p.ray;
		const vec3 dirSign = calcDirSign(ray.dir);
		const int chunkIndex = p.chunkIndex;
		const uint type = p.type;
		const int startBias = p.bias;
		const int parent = p.parent;
		
		const bool shadow = type == 1 || type == 2;
		
		const vec3 startP = playerRelativePosition - vec2(playerWidth/2.0, 0           ).xyx;
		const vec3 endP   = playerRelativePosition + vec2(playerWidth/2.0, playerHeight).xyx;
		
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
			
			const vec2 uv = blockUv(localSpaceCoord, intersectionSide, dirSign); //clamp(vec2(
			//	dot(intersectionSide, localSpaceCoord.zxx),
			//	dot(intersectionSide, localSpaceCoord.yzy)
			//), 0.0001, 0.9999);
			
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
					#if 0
					ambient = lightForChunkVertexDir(intersectionChunkIndex, vertexCoord, normal);
					#else
					for(int i = 0 ; i < 4; i++) {
						const ivec2 offset_ = ivec2(i%2, i/2);
						const ivec3 offset = offset_.x * otherAxis1 + offset_.y * otherAxis2;
						const ivec3 curVertexCoord = vertexCoord + offset;
											
						const float vertexLight = lightForChunkVertexDir(intersectionChunkIndex, curVertexCoord, normal);
						
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
					float(bias >= 0) * lightingAtCube(intersectionChunkIndex, vertexCoord - negative_ * intersectionSide),
					float(bias <= 0) * lightingAtCube(intersectionChunkIndex, vertexCoord - positive_ * intersectionSide)
				);
			}
			
			
			if(blockId == 7) {	
				const vec3 offset = vec3( 
					sin(localSpaceCoord.x*9)/2, 
					sin(localSpaceCoord.z*9)/8, 
					sin(localSpaceCoord.y*6 + localSpaceCoord.x*4)/8
				) / 100;
				
				const vec3 newBlockCoord = localSpaceCoord - offset;
				
				const vec2 newUV = blockUv(newBlockCoord, intersectionSide, dirSign);
				const vec3 color_ = sampleAtlas(atlasOffset, clamp(newUV, 0.0001, 0.9999)) * mix(0.5, 1.0, ambient);
				const vec3 color = fade(color_, t);

				pushResult(Result( color, t, surfaceBlock(blockId), type, parent));
				if(canPushParams()) {
					const vec3 incoming = ray.dir - offset * vec3(1-intersectionSide);
				
					const vec3 refracted_ = refract(normalize(incoming), normalize(normal), 1 );
					const bool isRefracted = refracted_ != vec3(0);
					const vec3 newDir = isRefracted ? refracted_ : reflect(incoming, normalize(vec3(side)));
					const int newBias = sign( isRefracted ? bias + 1 : -bias );
				
					pushParams(Params( Ray(coord, newDir), intersectionChunkIndex, newBias, type, curFrame));
				}
				continue;
			}
			else if(blockId == 8) {
				const vec3 offset = normalize(texture(noise, uv/5).xyz - 0.5) * 0.01;
				
				const vec3 newBlockCoord = localSpaceCoord - offset;
				const vec2 newUv = blockUv(newBlockCoord, intersectionSide, dirSign);
				
				const vec3 color_ = sampleAtlas(atlasOffset, newUv) * light * mix(0.3, 1.0, ambient);
				const vec3 color = fade(color_, t);
				
				
				pushResult(Result( color, t, surfaceBlock(blockId), type, parent));
				if(canPushParams()) {
					const vec3 reflected_ = reflect( ray.dir, normalize( normal + offset ) );
					const vec3 reflected  = abs(reflected_) * normal + reflected_ * (1 - abs(normal));
				
					pushParams(Params( Ray(coord, reflected), intersectionChunkIndex, -bias, type, curFrame));
				}
				continue;
			}			
			else {
				const bool emitter = blockId == 13 || blockId == 14;
				const ivec3 relativeToChunk = chunkPosition(intersectionChunkIndex) - startChunkPosition;
				const vec3 color_ = sampleAtlas(atlasOffset, uv) * (emitter ? 1.0 : light * mix(0.3, 1.0, ambient));
				const vec3 color = fade(color_, t);

				pushResult(Result( color * (emitter ? 2 : 1), t, surfaceBlock(blockId), type, parent));
				
				if(!shadow && !emitter) {
					const int shadowOffsetsCount = 4;
					const float shadowOffsets[] = { -1, 0, 1, 0 };
					
					const int shadowSubdiv = 32;
					const float shadowSmoothness = 32;
					const int shadowsInChunkDim = blocksInChunkDim * shadowSubdiv;		
					
					const vec3 position = ( floor(coord * shadowSubdiv) + (1-abs(normal))*vec3(0.499, 0.5, 0.501) )/shadowSubdiv;
					
					const ivec3 shadowInChunkCoord = ivec3(floor(mod(coord * shadowSubdiv, vec3(shadowsInChunkDim))));
					const int shadowInChunkIndex = 
						  shadowInChunkCoord.x
						+ shadowInChunkCoord.y * shadowsInChunkDim
						+ shadowInChunkCoord.z * shadowsInChunkDim * shadowsInChunkDim; //should fit in 31 bit
					const int rShadowInChunkIndex = mapInteger(shadowInChunkIndex);
					const int dirIndex = int(uint(rShadowInChunkIndex) % 42);
					const bool emitters = dirIndex < 30;
					
					if(canPushParams()) { //shadow
						const vec3 offset_ = vec3(
							shadowOffsets[ int(mod(floor(coord.x * shadowSubdiv), shadowOffsetsCount)) ],
							shadowOffsets[ int(mod(floor(coord.y * shadowSubdiv), shadowOffsetsCount)) ],
							shadowOffsets[ int(mod(floor(coord.z * shadowSubdiv), shadowOffsetsCount)) ]
						);
						const vec3 offset = (dot(offset_, offset_) == 0 ? offset_ : normalize(offset_)) / shadowSmoothness;
						const vec4 q = vec4(normalize(vec3(-1-sin(time/4)/10,3,2)), cos(time/4)/5);
						const vec3 v = normalize(vec3(-1, 4, 2) + offset);
						const vec3 temp = cross(q.xyz, v) + q.w * v;
						const vec3 rotated = v + 2.0*cross(q.xyz, temp);
						
						const vec3 newDir = normalize(rotated);
						const int newBias = bias * int(sign(dot(ray.dir*newDir, intersectionSide)));
						
						pushParams(Params( Ray(position, newDir), intersectionChunkIndex, newBias, 1u, curFrame));
					}
					
					if(emitters && canPushParams()) {
						const NE ne = neFromChunk(intersectionChunkIndex, dirIndex);
						
						if(ne.is) {
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
							
							const vec2 random = vec2(rShadowInChunkIndex & 0xffff, int(uint(rShadowInChunkIndex) >> 16)) / 0xffff;
							
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
								pushParams(Params( Ray(position, newDir), intersectionChunkIndex, newBias, 2u, curFrame));
							}
						}
					}
				}
				
				continue;
			}
		}
		else if(isPlayer && i.chunkIndex != -1) { //player
			pushResult(Result( vec3(1), playerT, surfaceEntity(1), type, parent));
			if(!shadow && canPushParams()) {
				const vec4 q = vec4(normalize(vec3(1+sin(time/4)/10,3,2)), cos(time/4)/5);
				const vec3 v = normalize(vec3(-1, 4, 2));
				const vec3 temp = cross(q.xyz, v) + q.w * v;
				const vec3 rotated = v + 2.0*cross(q.xyz, temp);
				
				const vec3 newDir = normalize(rotated);
				const Ray newRay = Ray( playerIntersection.at + playerIntersection.normal * 0.001, newDir );

				pushParams(Params( newRay, i.chunkIndex, 0, 1, curFrame));
			}
			continue;
		}
		else { //sky
			pushResult(Result( shadow ? vec3(1) : background(ray.dir), far, noSurface(), type, parent));
			continue;
		}
	}
		
	if(curResultPos() < 0) return vec3(1, 0, 1);
	
	for(int curFrame = curResultPos(); curFrame >= 0; curFrame--) {
		const Result currentResult = readResult(curFrame);
		const int currentParent = currentResult.parent;
		if(currentParent < 0) break;
		
		const Result outerResult = readResult(currentParent);

		const Result res = combineSteps(outerResult, currentResult);
		writeResult(res, currentParent);
	}	

	return readResult(0).color;
}

vec3 colorMapping(const vec3 col) {
	const float bw = 0.299 * col.r + 0.587 * col.g + 0.114 * col.b;
	
	const vec3 c = rgb2hsv(col);
	return hsv2rgb(vec3(
		c.x,
		(c.y+0.03) / pow(c.z+1.0, 1.0 / 10),
		pow(log(c.z + 1.0) / log(bw + 2.35), 1.2)
	));
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
	
	const vec3 c2 = colorMapping(col);
	
	if(length(gl_FragCoord.xy - windowSize / 2) < 3) color = vec4(vec3(0.98), 1);
	else color = vec4(c2, 1);
}	