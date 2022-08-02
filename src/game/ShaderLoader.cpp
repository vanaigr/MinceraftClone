#include "ShaderLoader.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop
#include<vector>
#include<string>
#include <iostream>
#include <fstream>

GLuint ShaderLoader::addShaderFromCode(std::string_view const shaderCode, unsigned int const shaderType, std::string_view const name) {
	auto const shaderId{ glCreateShader(shaderType) };

	char const *const begin{ &shaderCode[0] };
	GLint const length( shaderCode.length() );
	glShaderSource(shaderId, 1, &begin, &length);
	glCompileShader(shaderId);

	GLint result;
	glGetShaderiv(shaderId, GL_COMPILE_STATUS, &result);

	if (result == GL_FALSE) {
		int length;
		glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &length);

		std::unique_ptr<GLchar[]> strInfoLog{ new GLchar[length + 1] };
		glGetShaderInfoLog(shaderId, length, &length, strInfoLog.get());
		
		std::cout << "Compilation error in shader `" << name << "`:\n" << strInfoLog.get() << '\n';
	}

	shaders.push_back(shaderId);
	return shaderId;
}

GLuint ShaderLoader::addShaderFromProjectFileName(std::string const &fileName, unsigned int const shaderType, std::string_view const name) {
	std::ifstream file(fileName); //doesn't take string view
	if (file.fail()) std::cout << name << ": Shader file does not exist: " << fileName << '\n';
	std::string const shaderCode{ (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>() };
	return this->addShaderFromCode(shaderCode, shaderType, name);
}

void ShaderLoader::attachShaders(const unsigned int programId) {
	for(auto const it : shaders) {
		glAttachShader(programId, it);
	}
}
void ShaderLoader::deleteShaders() {
	for(auto const it : shaders) {
		glDeleteShader(it);
	}
	shaders.clear();
}