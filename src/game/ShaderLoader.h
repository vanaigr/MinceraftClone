#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wlanguage-extension-token"
      #include <GL/glew.h>
  #pragma clang diagnostic pop
#else
    #include <GL/glew.h>
#endif

#include<string_view>
#include<vector>

class ShaderLoader {
private:
	std::vector<unsigned int> shaders;
public:
	ShaderLoader() = default;
	
	GLuint addShaderFromCode(std::string_view const shaderCode, const unsigned int shaderType, std::string_view const name);
	GLuint addShaderFromProjectFileName(std::string const &filename, const unsigned int shaderType, std::string_view const name);

	void attachShaders(GLuint const programId);
	void deleteShaders();
};


