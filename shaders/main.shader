#version 430

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

#define epsilon 0.0001
#define eps1m vec2(1.0 - epsilon, 1.0 - epsilon)


uniform uvec2 windowSize;

uniform vec3 rightDir, topDir;

in vec4 gl_FragCoord;
out vec4 color;

uint fragCount = 4;

uniform float time;

uniform sampler2D atlas;
uniform vec2 atlasTileCount;

uniform float mouseX;

uniform mat4 projection; //from local space to screen

in vec3 vertColor;

uniform bool chunkNew;

uniform vec3 relativeChunkPos;


in vec3 color_;

layout(binding = 1) restrict readonly buffer ChunkIndices {
     uint data[16*16*16/2]; //indeces are shorts 	//8192
};

struct Block {
	uint id;
};
layout(binding = 2) restrict readonly buffer Blocks {
    Block blocks[];
};


struct Ray {
    vec3 orig;
    vec3 dir;
};

vec3 sampleAtlas(const vec2 offset, const vec2 coord) {
    vec2 uv = vec2(
        coord.x + offset.x,
        coord.y + atlasTileCount.y - (offset.y + 1)
    ) / atlasTileCount;
    return texture2D(atlas, uv).rgb;
    //return vec3(t, 0, 0 );
}

bool checkBoundaries(const ivec3 i) {
	#define ch16(i) (i >= 0 && i < 16)
		return ch16(i.x) && ch16(i.y) && ch16(i.z);
	#undef ch16
}

bool isIntersection(const ivec3 i_v) {
	const int index = i_v.x + i_v.y * 16 + i_v.z * 16 * 16;
	const int packedIndex = index / 2;
	const int offset = (index % 2) * 16;
	const uint id = (data[packedIndex] >> offset) & 65535;
	return id != 0;
}

uint blockAt(const ivec3 i_v) {
	const int index = i_v.x + i_v.y * 16 + i_v.z * 16 * 16;
	const int packedIndex = index / 2;
	const int offset = (index % 2) * 16;
	const uint id = (data[packedIndex] >> offset) & 65535;
	return id;
}

bool isIntersection_s(const ivec3 i_v) {
	return checkBoundaries(i_v) && isIntersection(i_v);
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
	float t;
	ivec3 index;
	uint id;
	vec2 uv;
	ivec3 side;
};


struct Optional_BlockIntersection {
	bool is;
	BlockIntersection it;
};

Optional_BlockIntersection empty_Optional_BlockIntersection() {
	Optional_BlockIntersection a;
	return a;
}


//Optional_BlockIntersection    (const) Ray, 'coord along', 'other coords'
#define              checkPlane(ray        , ca           , co            ) {                                                   \
	const bool dir = ray.dir.##ca > 0;                                                                                          \
	const int dir_ = int(sign(ray.dir.##ca));                                                                                   \
	const int positive = max(dir_, 0);                                                                                   		\
	const int negative = max(-dir_, 0);                                                                                   		\
	if(dir_ == 0) return empty_Optional_BlockIntersection();                                                                    \
	                                                                                                                            \
	const float unscale = 1.0 / ray.dir.##ca;                                                                                   \
	const vec2 step = ray.dir.##co * unscale;                                                                                   \
	                                                                                                                            \
	const int nearest = clamp(int(ceil(ray.orig.##ca * dir_) * dir_), 0, 16);                                                   \
	                                                                                                                            \
	const vec2 startCoords = ray.orig.##co + (nearest - ray.orig.##ca) * step;                                                  \
	                                                                                                                            \
	/*try to move ray inside the chunk bounds if in lies ooutide of them*/                                                      \
	const vec2 toB1 = -max(startCoords - 16, 0)/step*dir_;                                                                      \
	const vec2 toB2 = max(-startCoords, 0)     /step*dir_;                                                                      \
																																\
	/*minimum number of steps required for the ray to be in bounds*/                                                            \
	const ivec2 minimumSteps = ivec2(ceil(max(toB1, toB2)));                                                              		\
	const int minumumStep = max(minimumSteps.x, minimumSteps.y);                                                                \
																																\
	/*check if any of the axis requires negative number of steps to be in bounds*/                                              \
	/*const vec2 negativeSteps = floor(min(toB1, toB2));                                                                   		\
	if(any(lessThan(negativeSteps, vec2(0,0)))) return empty_Optional_BlockIntersection();*/									\
																																\
	const int startStep = nearest + minumumStep * dir_;																			\
																																\
	if(startStep-negative < 0 || startStep+positive > 16) return empty_Optional_BlockIntersection();							\
	const ivec3 signOfDir = ivec3(sign(ray.dir));                                                                               \
	const ivec3 farBoundaries = max(signOfDir, 0) * 17 - 1;                                                                     \
	for(int i = startStep; i != 16*positive; i+= dir_) {                                                                    	\
		ivec3 index;                                                                                                            \
		index.##ca = i - negative;                                                                            					\
		const vec2 other = startCoords + step * (i-nearest);																	\
		index.##co = ivec2(floor(other));                                                    									\
		if(all(lessThan((index - farBoundaries) * signOfDir, ivec3(0,0,0)))) {                                             		\
			const uint block = blockAt(index);																					\
			if(block!=0) {																										\
				ivec3 side = ivec3(0,0,0);                          								                            \
				side.##ca = -dir_;                                                                                              \
				return Optional_BlockIntersection(                                                                              \
					true,                                                                                                       \
					BlockIntersection(                                                                                          \
						unscale * (i - ray.orig.##ca),                     														\
						index,                                                                                                  \
						block,																									\
						mod(startCoords + step * (i-nearest), 1.0),                                                             \
						side                                                                                                    \
					)                                                                                                           \
				);                                                                                                              \
			}                                                                                                                   \
		} else break;                                                                       									\
	}                                                                                                                           \
	return empty_Optional_BlockIntersection();                                                                                  \
}


Optional_BlockIntersection checkX(const Ray ray) { /*return*/ checkPlane(ray, x, zy); }
Optional_BlockIntersection checkY(const Ray ray) { /*return*/ checkPlane(ray, y, xz); } 
Optional_BlockIntersection checkZ(const Ray ray) { /*return*/ checkPlane(ray, z, xy); }

#define check(axis, min_intersection, min_t) {\
	const Optional_BlockIntersection tmp = check##axis##(ray);\
	if(tmp.is) {\
		if(min_t > tmp.it.t) {\
			min_intersection = tmp.it;\
			min_t = tmp.it.t;\
		}\
	}\
}

void main() {
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const Ray ray = Ray(-relativeChunkPos, rayDir);
	
	BlockIntersection min_intersection;
	float min_t = 1.0 / 0.0;
	
	check(X, min_intersection, min_t);
	check(Y, min_intersection, min_t);
	check(Z, min_intersection, min_t);
	
	vec4 col;
	if(min_t < 1000) {
		const BlockIntersection i = min_intersection;
		const vec2 uv = i.uv;
		bool isTop = i.side.y ==  1;
		bool isBot = i.side.y == -1;
		col = vec4(
				mix(
				mix(
					sampleAtlas(vec2(0, 0), uv.xy),
					sampleAtlas(vec2(1, 0), uv.xy),
					float(isTop)
				),
				sampleAtlas(vec2(2, 0), uv.xy),
				float(isBot)
				), 1);
		if(i.id == 2) {
			col = mix(col, vec4(1, 0, 0, 1), 0.1);
		}
		
		//color = vec4(uv, 0, 1);
	}
	else col = vec4(0,0,0,0);
	
	//col = vec4(min_intersection.test, 1);
	
	//if(chunkNew) col = mix(col, vec4(1,0,0,1),0.3);
	
	color = col;
}