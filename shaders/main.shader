#version 430

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

#define epsilon 0.0001
#define eps1m vec2(1.0 - epsilon, 1.0 - epsilon)


uniform uvec2 windowSize;

uniform vec3 position;
uniform vec3 rightDir, topDir;

in vec4 gl_FragCoord;
out vec4 color;

uint fragCount = 4;

uniform float time;

//uniform sampler2D bot;
//uniform sampler2D side;
//uniform sampler2D top;
uniform sampler2D atlas;
uniform vec2 atlasTileCount;

#define side vec2(0, 0)
#define top vec2(1, 0)
#define bot vec2(2, 0)

uniform float mouseX;

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

layout(std430, binding = 1) buffer terrain {
    vec3 pos[];
};

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float map(float value, float min1, float max1, float min2, float max2) {
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

bool intersect(const Ray r, const vec3 min, const vec3 max, out vec3 mins, out vec3 maxs)
{
    float txmin = (min.x - r.orig.x) / r.dir.x;
    float txmax = (max.x - r.orig.x) / r.dir.x;

    if (txmin > txmax) swap(txmin, txmax);

    float tymin = (min.y - r.orig.y) / r.dir.y;
    float tymax = (max.y - r.orig.y) / r.dir.y;

    if (tymin > tymax) swap(tymin, tymax);

    if ((txmin > tymax) || (tymin > txmax))
        return false;

    if (tymin > txmin)
        txmin = tymin;

    if (tymax < txmax)
        txmax = tymax;

    float tzmin = (min.z - r.orig.z) / r.dir.z;
    float tzmax = (max.z - r.orig.z) / r.dir.z;

    if (tzmin > tzmax) swap(tzmin, tzmax);

    if ((txmin > tzmax) || (tzmin > txmax))
        return false;

    if (tzmin > txmin)
        txmin = tzmin;

    if (tzmax < txmax)
        txmax = tzmax;

    mins.x = txmin;
    mins.y = tymin;
    mins.z = tzmin;

    maxs.x = txmax;
    maxs.y = tymax;
    maxs.z = tzmax;
    return true;
}

float intersectPlane(const Ray r, const vec3 center, const vec3 n) {
    return dot(n, center - r.orig) / dot(n, r.dir);
}

float intersectSquare(const Ray r, const vec3 center, const vec3 n, const vec3 up, const vec3 left, const float radius, inout vec2 uv) {
    const vec3 c = center + n * radius; //plane center
    const float t = intersectPlane(r, c, n);
    const vec3 p = at(r, t); //point
    vec3 l = p - c; //point local to plane
    float u_ = dot(l, up);
    float v_ = dot(l, left);
    if (abs(u_) <= radius && abs(v_) <= radius) {
        uv = (vec2(u_, v_) / 2 / radius + 0.5) * eps1m;
        return t;
    }
    return 1.0 / 0.0;
}

float intersectCube(const Ray ray, const vec3 center, const vec3 n1, const vec3 n2, const float size, out vec3 n_out, out vec2 uv_out) {
    const vec3 n3 = cross(n2, n1);

    const vec3 ns[3] = { 
         n1 * -sign( dot(n1, ray.dir) )
        ,n2 * -sign( dot(n2, ray.dir) )
        ,n3 * -sign( dot(n3, ray.dir) )
    };

    vec2 uvs[3];

    const uint sides = 3;
    const float arr[sides] = {
         intersectSquare(ray, center, ns[0], n3, n2, size / 2, uvs[0])
        ,intersectSquare(ray, center, ns[1], n1, n3, size / 2, uvs[1])
        ,intersectSquare(ray, center, ns[2], n1, n2, size / 2, uvs[2])
        
        // SquareInfo(intersectSquare(r, center,  n1, n2, n3, size),  n1)
        //,SquareInfo(intersectSquare(r, center, -n1, n2, n3, size), -n1)
        //,SquareInfo(intersectSquare(r, center,  n2, n1, n3, size),  n2)
        //,SquareInfo(intersectSquare(r, center, -n2, n1, n3, size), -n2)
        //,SquareInfo(intersectSquare(r, center,  n3, n2, n1, size),  n3)
        //,SquareInfo(intersectSquare(r, center, -n3, n2, n1, size), -n3)
    };

    float shortestT = 1.0 / 0.0;
    for (uint i = 0; i < sides; i++) {
        float t = arr[i];
        if (t > epsilon && t < shortestT) {
            shortestT = t;
            n_out = ns[i];
            uv_out = uvs[i];
        }
    }

    return shortestT;
}

vec3 background(const Ray ray) {
    const float t = 0.5 * (ray.dir.y + 1.0);
    return (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
}

bool trace_scene(const Ray ray, out vec2 uv_out, out vec3 n_out) {
    vec3 n;
    float minT = 1.0 / 0.0;
    uint minI = 0;
    vec2 uv;
    for (uint i = 0; i < pos.length(); i++) {
        vec3 normal_;
        vec2 uv_;
        float t = intersectCube(ray, pos[i], normalize(vec3(1, 0, 0)), normalize(vec3(0, 1, 0)), 1, normal_, uv_);
        if (t < minT) {
            minT = t;
            n = normal_;
            minI = i;
            uv = uv_;
        }
    }

    if (minT < 1000) {
        uv_out = uv;
        n_out = n;
        return true;
    }
    else {
        return false;
    }
}

float rand(const vec2 co) {
    return fract(sin(dot(co + vec2(time, time), vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 trace(vec2 coord_) {
    vec2 coord = (coord_.xy / windowSize.xy) * 2 - 1;

    vec3 forwardDir = cross(topDir, rightDir);
    vec3 rayDir = rightDir * coord.x + topDir * coord.y + forwardDir * 1;
    rayDir = normalize(rayDir);

    Ray ray = Ray(position, rayDir);

    vec3 col;

    //for (uint i = 0; i < 1; ++i) {
    vec2 uv;
    vec3 n;
        if (trace_scene(ray, uv, n)) {
            bool isTop = dot(n, vec3(0, 1, 0)) > 0.5;
            bool isBot = dot(n, vec3(0, 1, 0)) < -0.5;
            col = mix(
               mix(
                   sampleAtlas(side, uv.xy),
                   sampleAtlas(top, uv.xy),
                   float(isTop)
               ),
                sampleAtlas(bot, uv.xy),
               float(isBot)
            );
            //col = texture2D(top, uv.xy).rgb;
            //col = n;
            //col = vec3(uv, 0);
            //ray.dir = normalize(ray.dir + .02 * vec3(
            //    rand(coord_.xy), rand(coord_.xy * 1.3), rand(coord_.xy * 1.2)
            //));
        }
        else {
            col = background(ray);
            //break;
        }
    //}

    return col;
}

vec3 sampleN(const vec2 coord, const uint n) {
    const vec2 pixelCoord = floor(coord);
    const float fn = float(n);

    vec3 result = vec3(0, 0, 0);
    for (uint i = 0; i < n; i++) {
        for (uint j = 0; j < n; j++) {
            const vec2 offset = vec2(rand(coord.xy), rand(coord.xy + pixelCoord)) / fn / 2;
            const vec2 coord = pixelCoord + vec2(0.5, 0.5) + (vec2(i / fn, j / fn) - vec2(0.5, 0.5)) + offset;

            const vec3 sampl = trace(coord);
            result += sampl;
        }
    }

    return result / (fn * fn);
}

void main(void) {
    //color = vec4(sampleN(gl_FragCoord.xy, 2), 1);
    color = vec4(trace(gl_FragCoord.xy), 1);
}
