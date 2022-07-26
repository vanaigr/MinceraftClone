#version 430

layout(std140) uniform Properties {
	ivec2 windowSize;
	float time;
	mat4 projection; //from local space to screen space
};

//ERROR '=' : global const initializers must be constant ' const float'
/*const*/ float aspect = float(windowSize.y) / windowSize.x;
/*const*/ vec2 size = vec2(aspect, 1) * 0.06;
/*const*/ vec2 end_ = vec2(1 - 0.02 * aspect, 1-0.02);
/*const*/ vec2 start_ = vec2( end_ - size );
/*     */
/*const*/ vec2 startPos_ = floor(start_ * windowSize) / windowSize * 2 - 1;
/*const*/ vec2 endPos_   = floor(end_   * windowSize) / windowSize * 2 - 1;

out vec2 startPos;
out vec2 endPos;

void main() {
	startPos = startPos_;
	endPos = endPos_;
	const vec2 verts[] = {
		vec2(0),
		vec2(1, 0),
		vec2(0, 1),
		vec2(1)
	};
	vec2 interp = verts[gl_VertexID];
	gl_Position = vec4(mix(startPos_, endPos_, interp), 0, 1);
}