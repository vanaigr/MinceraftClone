#pragma once
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop
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
	GLuint addShaderFromProjectFileName(const std::string& filename, const unsigned int shaderType, const std::string& name = "");

	void attachShaders(const unsigned int programId);
	void deleteShaders();
};


