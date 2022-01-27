#version 430

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

#define epsilon 0.0001
#define eps1m vec2(1.0 - epsilon, 1.0 - epsilon)


uniform uvec2 windowSize;

//uniform vec3 position;
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

layout(packed, binding = 1) uniform Chunk {
	vec3 relativeChunkPos; 							//12
    uint data[16*16*16/2]; //indeces are shorts 	//8192
};

struct Block {
	uint id;
};

layout(binding = 2) buffer Blocks {
    Block blocks[]; //block 0 is special
};

bool ch16(const int i) {
	return (i >= 0 && i < 16);
}

bool checkBoundaries(const ivec3 i) {
	return ch16(i.x) && ch16(i.y) && ch16(i.z);
}

bool isIntersection(const ivec3 i_v) {
	int index = i_v.x + i_v.y * 16 + i_v.z * 16 * 16;
	int packedIndex = index / 2;
	int offset = (index % 2) * 16;
	uint id = (data[packedIndex] >> offset) & 65535;
	return id != 0;
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

/*bool checkX(const Ray ray, out ivec3 index_out) {
	const bool dir = ray.dir.x > 0;
	const int dir_ = int(sign(ray.dir.x));
	if(dir_ == 0) return false;
	
	const vec2 step = ray.dir.yz / ray.dir.x;
	
	const int nearest = clamp(int(ceil(ray.orig.x * dir_) * dir_), 0, 16);
	
	const vec2 startCoords = ray.orig.yz + (nearest - ray.orig.x) * step;
	
	for(int i = nearest; i != (dir ? 16 : 0); i+= dir_) {
			const ivec3 index = ivec3(
				min(i, 15),
				ivec2(floor(startCoords + step * (i-nearest)))
			);
			Block out_block;
			if(isIntersection_s(index, out_block)) {
				index_out = index;
				return true;
			}
		}
	
	return false;
}*/

struct BlockIntersection {
	float t;
	ivec3 index;
	vec2 uv;
	ivec3 side;
};

//                 (const) Ray, (out) BlockIntersection, 'coord along', 'other coords'
#define checkPlane(ray        , intersection_out       , ca           , co            ) {\
	const bool dir = ray.dir.##ca > 0;\
	const int dir_ = int(sign(ray.dir.##ca));\
	if(dir_ == 0) return false;\
	\
	const float unscale = 1.0 / ray.dir.##ca;\
	const vec2 step = ray.dir.##co * unscale;\
	\
	const int nearest = clamp(int(ceil(ray.orig.##ca * dir_) * dir_), 0, 16);\
	\
	const vec2 startCoords = ray.orig.##co + (nearest - ray.orig.##ca) * step;\
	for(int i = nearest; i != (dir ? 16 : 0); i+= dir_) {\
			ivec3 index;\
			if(dir)index.##ca = min(i, 15);\
			else index.##ca = max(i-1, 0);\
			index.##co = ivec2(floor(startCoords + step * (i-nearest)));\
			if(isIntersection_s(index)) {\
				ivec3 side = ivec3(0,0,0);\
				side.##ca = -dir_;\
				intersection_out = BlockIntersection(\
					unscale * ((i-nearest) + (nearest - ray.orig.##ca))/*length(step * (i-nearest));*/,\
					index,\
					mod(startCoords + step * (i-nearest), 1.0),\
					side\
				);\
				return true;\
			}\
		}\
	\
	return false;\
}


bool checkX(const Ray ray, out BlockIntersection intersection_out) {
	/*return*/ checkPlane(ray, intersection_out, x, zy);
}
bool checkY(const Ray ray, out BlockIntersection intersection_out) {
	/*return*/ checkPlane(ray, intersection_out, y, xz);
}
bool checkZ(const Ray ray, out BlockIntersection intersection_out) {
	/*return*/ checkPlane(ray, intersection_out, z, xy);
}

void main() {
    const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
	
	const vec3 forwardDir = cross(topDir, rightDir);
    const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
    const vec3 rayDir = normalize(rayDir_);
	
	const Ray ray = Ray(-relativeChunkPos, rayDir);
	
	BlockIntersection min_intersection;
	BlockIntersection cur_intersection;
	float min_t = 1.0 / 0.0;
	if(checkX(ray, cur_intersection)) {
		if(min_t > cur_intersection.t) {
			min_intersection = cur_intersection;
			min_t = cur_intersection.t;
		}
	}
	if(checkY(ray, cur_intersection)) {
		if(min_t > cur_intersection.t) {
			min_intersection = cur_intersection;
			min_t = cur_intersection.t;
		}
	}
	if(checkZ(ray, cur_intersection)) {
		if(min_t > cur_intersection.t) {
			min_intersection = cur_intersection;
			min_t = cur_intersection.t;
		}
	}
	
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
		
		//color = vec4(uv, 0, 1);
	}
	else col = vec4(0,0,0,0);
	
	//if(chunkNew) col = mix(col, vec4(1,0,0,1),0.3);
	
	color = col;
}