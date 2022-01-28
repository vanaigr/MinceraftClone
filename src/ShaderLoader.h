#pragma once
#include <GLEW/glew.h>

#include<string>
#include<memory>

class ShaderLoader
{
private:
	struct Impl;
	std::unique_ptr<Impl> pimpl;
public:
	ShaderLoader();
	~ShaderLoader();

public:
	GLuint addShaderFromCode(const std::string& shaderCode, const unsigned int shaderType, const std::string& name = "");
	GLuint addShaderFromProjectFileName(const std::string& shaderCode, const unsigned int shaderType, const std::string& name = "");

	GLuint addScreenSizeTriangleStripVertexShader(const std::string& name = "");
	GLuint addScreenCoordFragmentShader(const std::string& name = "");

	void attachShaders(const unsigned int programId);
	void deleteShaders();
};


inline GLuint ShaderLoader::addScreenSizeTriangleStripVertexShader(const std::string& name) {
	return this->addShaderFromCode(
		"\n#version 300 es"
		"\nprecision mediump float;"
		"\nvoid main(void){"
		"\ngl_Position = vec4("
		"\n    2 * (gl_VertexID / 2) - 1,"
		"\n    2 * (gl_VertexID % 2) - 1,"
		"\n    0.0,"
		"\n    1.0);"
		"\n}"
		,
		GL_VERTEX_SHADER,
		name
	);
}

inline GLuint ShaderLoader::addScreenCoordFragmentShader(const std::string& name) {
	return this->addShaderFromCode(
		"\n#version 300 es"
		"\nprecision mediump float;"
		"\nlayout(origin_upper_left) in vec4 gl_FragCoord;"
		"\nout vec4 color;"
		"\nvoid main(void){"
		"\ncolor = gl_FragCoord;"
		"\n}"
		,
		GL_FRAGMENT_SHADER,
		name
	);
};

