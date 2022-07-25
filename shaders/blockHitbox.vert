#version 420

uniform mat4 projection;
uniform mat4 modelMatrix;

out vec2 uv;

//https://gist.github.com/rikusalminen/9393151
void main() {
	int tri = gl_VertexID / 3;
	int idx = gl_VertexID % 3;
	int face = tri / 2;
	int top = tri % 2;

	int dir = face % 3;
	int pos = face / 3;

	int nz = dir >> 1;
	int ny = dir & 1;
	int nx = 1 ^ (ny | nz);

	vec3 d = vec3(nx, ny, nz);
	float flip = 1 - 2 * pos;

	vec3 n = flip * d;
	vec3 u = -d.yzx;
	vec3 v = flip * d.zxy;

	float mirror = -1 + 2 * top;
	vec3 xyz = n + mirror*(1-2*(idx&1))*u + mirror*(1-2*(idx>>1))*v;
	xyz = (xyz + 1) / 2;

	gl_Position = projection * (modelMatrix * vec4(xyz, 1.0));
	uv = (vec2(mirror*(1-2*(idx&1)), mirror*(1-2*(idx>>1)))+1) / 2;
}