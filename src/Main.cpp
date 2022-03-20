#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop

#include <GLFW/glfw3.h>

#include"Vector.h"
#include"Chunks.h"
#include"ChunkCoord.h"
#include"Viewport.h"

#include"Font.h"
#include"ShaderLoader.h"
#include"Read.h"
#include"Misc.h"
#include"PerlinNoise.h"
#include"MeanCounter.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include<iostream>
#include<chrono>
#include<optional>
#include<vector>
#include<array>
#include<limits>
#include<tuple>
#include<sstream>
#include<fstream>

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2<uint32_t> windowSize{ 1920, 1080 };
#else
static const vec2<uint32_t> windowSize{ 1280, 720 };
#endif // FULLSCREEN

GLFWwindow* window;

static const vec2<double> windowSize_d{ windowSize.convertedTo<double>() };

static vec2<double> mousePos(0, 0), pmousePos(0, 0);

static bool shift{ false }, ctrl{ false };

static bool isPan{ false };

static bool debug{ false };

static double const height{ 1.95 };
static double const width{ 0.6 };

static int64_t const width_i{ ChunkCoord::posToFracRAway(width).x }; 
static int64_t const height_i{ ChunkCoord::posToFracRAway(height).x };

static double const deltaTime{ 16.0/1000.0 };
static double const fixedDeltaTime{ 16.0/1000.0 };

static double speedModifier = 2.5;
static double playerSpeed{ 2.7 };
static double spectatorSpeed{ 0.2 };

static double const aspect{ windowSize_d.y / windowSize_d.x };

static bool isOnGround{false};

static vec3d playerForce{};
static ChunkCoord playerCoord{ vec3i{0,0,0}, vec3d{0.01,12.001,0.01} };
static Viewport playerViewport{ 
	vec2d{ misc::pi / 2.0, 0 },
	aspect,
	90.0 / 180.0 * misc::pi,
	0.001,
	400
};

static ChunkCoord spectatorCoord{ playerCoord };
static Viewport spectatorViewport{ 
	vec2d{ misc::pi / 2.0, 0 },
	aspect,
	90.0 / 180.0 * misc::pi,
	0.001,
	400
};

static vec3d const viewportOffset_{0,height*0.9,0};
static bool isSpectator{ false };

static Viewport &viewport_current() {
    if(isSpectator) return spectatorViewport;
	return playerViewport;
}

static ChunkCoord &currentCoord() {
	if(isSpectator) return spectatorCoord;
	return playerCoord;
}

static vec3d viewportOffset() {
	return viewportOffset_ * (!isSpectator);
}

struct Input {
	vec3i movement;
	vec2d panning;
	bool jump;
};

static Input playerInput, spectatorInput;

Input &currentInput() {
	if(isSpectator) return spectatorInput;
	else return playerInput;
}

static void reloadShaders();

static bool testInfo = false;
static bool debugBtn0 = false, debugBtn1 = false;


bool mouseCentered = true;

enum class BlockAction {
	NONE = 0,
	PLACE,
	BREAK
} static blockAction{ BlockAction::NONE };

static double const blockActionCD{ 300.0 / 1000.0 };

static int blockId = 1;

static void quit();

enum class Key : uint8_t { RELEASE = GLFW_RELEASE, PRESS = GLFW_PRESS, REPEAT = GLFW_REPEAT, NOT_PRESSED };
static_assert(GLFW_RELEASE >= 0 && GLFW_RELEASE < 256 && GLFW_PRESS >= 0 && GLFW_PRESS < 256 && GLFW_REPEAT >= 0 && GLFW_REPEAT < 256);
static Key keys[GLFW_KEY_LAST+1];

void handleKey(int const key) {
	auto const action{ misc::to_underlying(keys[key]) };
	
	bool isPress = !(action == GLFW_RELEASE);
	if(key == GLFW_KEY_GRAVE_ACCENT && !isPress) {
		mouseCentered = !mouseCentered;
		if(mouseCentered) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	if(key == GLFW_KEY_ESCAPE && !isPress) {
		quit();
	}
	if(key == GLFW_KEY_W)
		currentInput().movement.z = 1 * isPress;
	else if(key == GLFW_KEY_S)
		currentInput().movement.z = -1 * isPress;
	
	if(isSpectator) {
		if (key == GLFW_KEY_Q)
			currentInput().movement.y = 1 * isPress;
		else if(key == GLFW_KEY_E)
			currentInput().movement.y = -1 * isPress;
	}
	else if(key == GLFW_KEY_SPACE) {
		currentInput().jump = isPress;
	}
		
	if (key == GLFW_KEY_KP_0 && !isPress) 
			debugBtn0 = !debugBtn0;
	if (key == GLFW_KEY_KP_1 && !isPress) 
		debugBtn1 = !debugBtn1;	

	if(key == GLFW_KEY_D)
		currentInput().movement.x = 1 * isPress;
	else if(key == GLFW_KEY_A)
		currentInput().movement.x = -1 * isPress;
	
	if(key == GLFW_KEY_F5 && action == GLFW_PRESS)
		reloadShaders();
	else if(key == GLFW_KEY_F4 && action == GLFW_PRESS)
		debug = !debug;
	else if(key == GLFW_KEY_F3 && action == GLFW_RELEASE) { 
		isSpectator = !isSpectator;
		if(isSpectator) {
			spectatorCoord = playerCoord + viewportOffset_;
			spectatorViewport.rotation = playerViewport.rotation;
		}
	}
	else if(key == GLFW_KEY_TAB && action == GLFW_PRESS) testInfo = true;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if(key == GLFW_KEY_UNKNOWN) return;
	if(action == GLFW_REPEAT) return;
	
	if(action == GLFW_PRESS) keys[key] = Key::PRESS;
	else if(action == GLFW_RELEASE) keys[key] = Key::RELEASE;
	
	handleKey(key);
	
	shift = (mods & GLFW_MOD_SHIFT) != 0;
    ctrl = (mods & GLFW_MOD_CONTROL) != 0;
}

static void cursor_position_callback(GLFWwindow* window, double mousex, double mousey) {
	static vec2d pMouse{0};
    static vec2<double> relativeTo{ 0, 0 };
    vec2<double> mousePos_{ mousex,  mousey };
    
	if(mouseCentered) {
		relativeTo += mousePos_ - pMouse;
	} else {
		relativeTo += mousePos_;
		mousePos_.x = misc::modf(mousePos_.x, windowSize_d.x);
		mousePos_.y = misc::modf(mousePos_.y, windowSize_d.y);
		relativeTo -= mousePos_;
		glfwSetCursorPos(window, mousePos_.x, mousePos_.y);
	}
	
	pMouse = mousePos_;
    mousePos = vec2<double>(relativeTo.x + mousePos_.x, relativeTo.y + mousePos_.y);
	
	if(isPan || mouseCentered) currentInput().panning += (mousePos - pmousePos) / windowSize_d;
	pmousePos = mousePos;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if(button == GLFW_MOUSE_BUTTON_LEFT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::BREAK : BlockAction::NONE;
		else isPan = action != GLFW_RELEASE;
	}
	else if(button == GLFW_MOUSE_BUTTON_RIGHT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::PLACE : BlockAction::NONE;
	}
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	blockId = 1+misc::mod(blockId-1 + int(yoffset), 9);
}

static const Font font{ ".\\assets\\font.txt" };

static size_t const texturesCount = 4;
static GLuint textures[texturesCount];
static GLuint &atlas_t = textures[0], &font_t = textures[1], &framebufferColor_t = textures[2], &framebufferDepth_t = textures[3];

static GLuint framebuffer;

static GLuint mainProgram = 0;
  static GLuint rightDir_u;
  static GLuint topDir_u;
  static GLuint near_u, far_u;
  static GLuint startChunkIndex_u;
  static GLuint time_u;
  static GLuint mouseX_u, mouseY_u;
  static GLuint projection_u, toLocal_matrix_u;
  static GLuint playerChunk_u, playerInChunk_u, chunksPostions_ssbo, chunksBounds_ssbo, chunksNeighbours_ssbo;

static GLuint mapChunks_p;

static GLuint fontProgram;

static GLuint testProgram;
	static GLuint tt_projection_u, tt_toLocal_u;
	
static GLuint debugProgram;
	static GLuint db_projection_u, db_toLocal_u, db_isInChunk_u;
	static GLuint db_playerChunk_u, db_playerInChunk_u;

static GLuint chunkIndex_u;
static GLuint blockSides_u;

static GLuint playerProgram = 0;

static GLuint pl_projection_u = 0;
static GLuint pl_modelMatrix_u = 0;

static int32_t gpuChunksCount = 0;
Chunks chunks{};

static int viewDistance = 3;

void resizeBuffer() {
	//assert(newGpuChunksCount >= 0);
	gpuChunksCount = chunks.used.size();
	auto &it = chunks.gpuPresent;
	
	it.assign(it.size(), false);
	
	static_assert(sizeof(chunks.chunksData[0]) == 8192);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkIndex_u);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * 8192, NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, chunkIndex_u);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksPostions_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(vec3i), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, chunksPostions_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, chunksBounds_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * Chunks::Neighbours::neighboursCount * sizeof(int32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, chunksNeighbours_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void reloadShaders() {
	glDeleteTextures(texturesCount, &textures[0]);
	glGenTextures(texturesCount, &textures[0]);
	
	{
		//color
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, framebufferColor_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, windowSize.x, windowSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		//depth
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, framebufferDepth_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, windowSize.x, windowSize.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		
		glDeleteFramebuffers(1, &framebuffer);
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebufferColor_t, 0);
		  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, framebufferDepth_t, 0);
		  
		  GLenum status;
		  if ((status = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE) {
		  	fprintf(stderr, "framebuffer: error %u", status);
		  }
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	
	{
		glDeleteProgram(mainProgram);
		mainProgram = glCreateProgram();
		ShaderLoader sl{};

		//sl.addShaderFromProjectFileName("shaders/vertex.shader",GL_VERTEX_SHADER,"main vertex");
		//https://gist.github.com/rikusalminen/9393151
		//... usage: glDrawArrays(GL_TRIANGLES, 0, 36), disable all vertex arrays
		sl.addShaderFromProjectFileName(
			"shaders/vertex.shader",
			GL_VERTEX_SHADER,
			"main vertex"
		);
		sl.addShaderFromProjectFileName("shaders/main.shader", GL_FRAGMENT_SHADER, "Main shader");
	
		sl.attachShaders(mainProgram);
	
		glLinkProgram(mainProgram);
		glValidateProgram(mainProgram);
	
		sl.deleteShaders();
	
		glUseProgram(mainProgram);
		
		glUniform2ui(glGetUniformLocation(mainProgram, "windowSize"), windowSize.x, windowSize.y);
		glUniform2f(glGetUniformLocation(mainProgram, "atlasTileCount"), 512 / 16, 512 / 16);
		
		time_u   = glGetUniformLocation(mainProgram, "time");
		mouseX_u = glGetUniformLocation(mainProgram, "mouseX");
		mouseY_u = glGetUniformLocation(mainProgram, "mouseY");
	
	
		rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
		topDir_u = glGetUniformLocation(mainProgram, "topDir");
		glUniform1f(glGetUniformLocation(mainProgram, "near"), playerViewport.near);
		glUniform1f(glGetUniformLocation(mainProgram, "far"), playerViewport.far);
		projection_u = glGetUniformLocation(mainProgram, "projection");
		toLocal_matrix_u = glGetUniformLocation(mainProgram, "toLocal");
		//chunk_u = glGetUniformLocation(mainProgram, "chunk");
	
		playerChunk_u = glGetUniformLocation(mainProgram, "playerChunk");
		playerInChunk_u = glGetUniformLocation(mainProgram, "playerInChunk");
		startChunkIndex_u = glGetUniformLocation(mainProgram, "startChunkIndex");
		
		glUniform1i(glGetUniformLocation(mainProgram, "worldColor"), 2);
		glUniform1i(glGetUniformLocation(mainProgram, "worldDepth"), 3);
	
		Image image;
		ImageLoad("assets/atlas.bmp", &image);
	
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, atlas_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data);
        //glBindTexture(GL_TEXTURE_2D, 0);
		
		GLuint const atlasTex_u = glGetUniformLocation(mainProgram, "atlas");
		glUniform1i(atlasTex_u, 0);
		
		{
			auto const c = [](int16_t const x, int16_t const y) -> int32_t {
				return int32_t( uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16) );
			}; //pack coord
			int32_t const sides[] = { //side, top, bot;
				c(0, 0), c(0, 0), c(0, 0), //0
				c(1, 0), c(2, 0), c(3, 0), //grass
				c(3, 0), c(3, 0), c(3, 0), //dirt
				c(4, 0), c(4, 0), c(4, 0), //planks
				c(5, 0), c(6, 0), c(6, 0), //wood
				c(7, 0), c(7, 0), c(7, 0), //leaves
				c(8, 0), c(8, 0), c(8, 0), //stone
				c(9, 0), c(9, 0), c(9, 0), //glass
				c(11, 0), c(11, 0), c(11, 0), //diamond
				c(12, 0), c(12, 0), c(12, 0), //obsidian
			};
			glGenBuffers(1, &blockSides_u);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, blockSides_u);
			glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(sides), &sides, GL_STATIC_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, blockSides_u);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
		/*GLuint ssbo;
		glGenBuffers(1, &ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(field), field, GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);*/
		
		/*GLuint chunkIndex_b = glGetUniformBlockIndex(mainProgram, "Chunk");
		glGenBuffers(1, &chunkUBO);
		glBindBuffer(GL_UNIFORM_BUFFER, chunkUBO);
		glUniformBlockBinding(mainProgram, chunkIndex_b, 1);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, chunkUBO);
		glBufferData(GL_UNIFORM_BUFFER, 12, NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);*/
		
		//relativeChunkPos_u = glGetUniformLocation(mainProgram, "relativeChunkPos");
		
		glDeleteBuffers(1, &chunkIndex_u);
		glGenBuffers(1, &chunkIndex_u);
		glDeleteBuffers(1, &chunksPostions_ssbo);
		glGenBuffers(1, &chunksPostions_ssbo);
		
		glDeleteBuffers(1, &chunksBounds_ssbo);
		glGenBuffers(1, &chunksBounds_ssbo);
		glDeleteBuffers(1, &chunksNeighbours_ssbo);
		glGenBuffers(1, &chunksNeighbours_ssbo);

		resizeBuffer();
	}
	
	{
		glDeleteProgram(fontProgram);
		fontProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromCode(
		R"(#version 300 es
			precision mediump float;
			
			layout(location = 0) in vec2 pos_s;
			layout(location = 1) in vec2 pos_e;
			layout(location = 2) in vec2 uv_s;
			layout(location = 3) in vec2 uv_e;
			
			out vec2 uv;
			void main(void){
				vec2 interp = vec2(gl_VertexID % 2, gl_VertexID / 2);
				gl_Position = vec4(mix(pos_s, pos_e, interp), 0, 1);
				uv = mix(uv_s, uv_e, interp);
			}
		)", GL_VERTEX_SHADER,"font vertex");
		
		sl.addShaderFromCode(
		R"(#version 420
			in vec4 gl_FragCoord;
			in vec2 uv;
			
			uniform sampler2D font;
			
			out vec4 color;
			void main() {
				const float col = texture2D(font, uv).r;
				color = vec4(vec3(0), 1-col*col);
				//color = vec4(col,col,col,1);
			}
		)",
		GL_FRAGMENT_SHADER,
		"font shader");
		
		sl.attachShaders(fontProgram);
	
		glLinkProgram(fontProgram);
		glValidateProgram(fontProgram);
	
		sl.deleteShaders();
	
		glUseProgram(fontProgram);
		
		Image image;
		ImageLoad("assets/font.bmp", &image);
	
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, font_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data);
        //glBindTexture(GL_TEXTURE_2D, 0);
		
		GLuint const fontTex_u = glGetUniformLocation(fontProgram, "font");
		glUniform1i(fontTex_u, 1);
	}
	
	{
		glDeleteProgram(testProgram);
		testProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromCode(
		R"(#version 420
			precision mediump float;
			uniform mat4 projection;
			uniform mat4 toLocal;
			
			layout(location = 0) in vec3 relativePos;
			layout(location = 1) in vec3 color_;
			
			out vec3 col;
			void main(void){
				//const mat4 translation = {
				//	vec4(1,0,0,0),
				//	vec4(0,1,0,0),
				//	vec4(0,0,1,0),
				//	vec4(relativePos, 1)
				//};			
		
				const mat4 model_matrix = toLocal;// * translation;
				
				gl_Position = projection * (model_matrix * vec4(relativePos, 1.0));
				col = color_;
			}
		)", GL_VERTEX_SHADER,"test vertex");
		
		sl.addShaderFromCode(
		R"(#version 420			
			out vec4 color;
			
			in vec3 col;
			void main() {
				color = vec4(col, 1);
			}
		)",
		GL_FRAGMENT_SHADER,
		"test shader");
		
		sl.attachShaders(testProgram);
	
		glLinkProgram(testProgram);
		glValidateProgram(testProgram);
	
		sl.deleteShaders();
		
		tt_toLocal_u = glGetUniformLocation(testProgram, "toLocal");
		tt_projection_u = glGetUniformLocation(testProgram, "projection");
	}
	
	{
		glDeleteProgram(debugProgram);
		debugProgram = glCreateProgram();
		ShaderLoader dbsl{};
		
		//sl.addShaderFromProjectFileName("shaders/vertex.shader",GL_VERTEX_SHADER,"main vertex");
		//https://gist.github.com/rikusalminen/9393151
		//... usage: glDrawArrays(GL_TRIANGLES, 0, 36), disable all vertex arrays
		dbsl.addShaderFromCode(R"(#version 430
			uniform mat4 projection;
			uniform mat4 toLocal;
			
			uniform ivec3 playerChunk;
			uniform  vec3 playerInChunk;
			
			in layout(location = 0) uint chunkIndex_;
			
			layout(binding = 3) restrict readonly buffer ChunksPoistions {
				int positions[];
			} ps;

			layout(binding = 4) restrict readonly buffer ChunksBounds {
				uint bounds[];
			} bs;
			
			ivec3 chunkPosition(const uint chunkIndex) {
				const uint index = chunkIndex * 3;
				return ivec3(
					ps.positions[index+0],
					ps.positions[index+1],
					ps.positions[index+2]
				);
			}
			
			uint chunkBounds(const uint chunkIndex) {
				return bs.bounds[chunkIndex];
			}
			
			//copied from Chunks.h
			#define chunkDim 16u

			vec3 indexBlock(const uint index) { //copied from Chunks.h
				return vec3( index % chunkDim, (index / chunkDim) % chunkDim, (index / chunkDim / chunkDim) );
			}
			vec3 start(const uint data) { return indexBlock(data&65535u); } //copied from Chunks.h
			vec3 end(const uint data) { return indexBlock((data>>16)&65535u); } //copied from Chunks.h
			vec3 onePastEnd(const uint data) { return end(data) + 1; } //copied from Chunks.h
			
			vec3 relativeChunkPosition(const uint chunkIndex) {
				return vec3( (chunkPosition(chunkIndex) - playerChunk) * chunkDim ) - playerInChunk;
			}
			
			void main()
			{
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
				
				const vec3 relativeChunkPos_ = relativeChunkPosition(chunkIndex_);
				const uint positions_ = chunkBounds(chunkIndex_);
				
				const mat4 translation = {
					vec4(1,0,0,0),
					vec4(0,1,0,0),
					vec4(0,0,1,0),
					vec4(relativeChunkPos_, 1)
				};			
		
				const mat4 model_matrix = toLocal * translation;
				
				const vec3 startPos = start(positions_);
				const vec3 endPos = onePastEnd(positions_);
				const vec3 position = mix(startPos, endPos, xyz);
			
				gl_Position = projection * (model_matrix * vec4(position, 1.0));
			}
		)", GL_VERTEX_SHADER, "debug vertex");
		dbsl.addShaderFromCode(
			R"(#version 420
			uniform bool isInChunk;
			out vec4 color;
			void main() {
				color = vec4(float(!isInChunk), 0, float(isInChunk), 1);
			})",
			GL_FRAGMENT_SHADER, "debug shader"
		);
	
		dbsl.attachShaders(debugProgram);
	
		glLinkProgram(debugProgram);
		glValidateProgram(debugProgram);
	
		dbsl.deleteShaders();
	
		glUseProgram(debugProgram);
		
		db_toLocal_u = glGetUniformLocation(debugProgram, "toLocal");
		db_projection_u = glGetUniformLocation(debugProgram, "projection");
		db_isInChunk_u = glGetUniformLocation(debugProgram, "isInChunk");
		
		db_playerChunk_u = glGetUniformLocation(debugProgram, "playerChunk");
		db_playerInChunk_u = glGetUniformLocation(debugProgram, "playerInChunk");
	}
	
	{
		glDeleteProgram(playerProgram);
		playerProgram = glCreateProgram();
		ShaderLoader plsl{};

		plsl.addShaderFromCode(
			R"(#version 420
			uniform mat4 projection;
			uniform mat4 model_matrix;
			
			out vec3 norm;
			void main()
			{
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
			
				gl_Position = projection * (model_matrix * vec4(xyz, 1.0));
				norm = n; //works for simple model matrix
			}
			)", GL_VERTEX_SHADER, "Player vertex"
		);
		
		plsl.addShaderFromCode(
			R"(#version 420
			in vec4 gl_FragCoord;
			out vec4 color;

			in vec3 norm;
			
			float map(const float value, const float min1, const float max1, const float min2, const float max2) {
				return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
			}

			void main() {
				const vec3 n = normalize(norm);
				const vec3 sun = normalize(vec3(1, 1, 1));
				color = vec4(
					vec3(1, 1, 1) * map(dot(n, sun), -1, 1, 0.6, 0.9),
					1
				);
			}
			)", 
			GL_FRAGMENT_SHADER,  
			"Player shader"
		);
	
		plsl.attachShaders(playerProgram);
	
		glLinkProgram(playerProgram);
		glValidateProgram(playerProgram);
	
		plsl.deleteShaders();
	
		glUseProgram(playerProgram);
		
		pl_projection_u = glGetUniformLocation(playerProgram, "projection");
		pl_modelMatrix_u = glGetUniformLocation(playerProgram, "model_matrix");
	}
	
	float projection[4][4];
	viewport_current().projectionMatrix(&projection);
	
	glUseProgram(playerProgram);
	glUniformMatrix4fv(pl_projection_u, 1, GL_TRUE, &projection[0][0]);
	 
	glUseProgram(debugProgram);
	glUniformMatrix4fv(db_projection_u, 1, GL_TRUE, &projection[0][0]);	
	
	glUseProgram(mainProgram);
	glUniformMatrix4fv(projection_u, 1, GL_TRUE, &projection[0][0]);
	
	glUseProgram(testProgram);
	glUniformMatrix4fv(tt_projection_u, 1, GL_TRUE, &projection[0][0]);
}

template<typename T, typename L>
inline void apply(size_t size, T &t, L&& l) {
	for(size_t i = 0; i < size; ++i) 
		if(l(t[i], i) == true) break;
}

template<typename T, int32_t maxSize, typename = std::enable_if<(maxSize>0)>>
struct CircularArray {
private:
	T arr[maxSize];
	uint32_t curIndex;
public:
	CircularArray() : curIndex{0} {};
	
	T *begin() { return arr[0]; }
	T *end() { return arr[size()]; }
	T const *cbegin() const { return arr[0]; }
	T const *cend() const { return arr[size()]; }
	
	template<typename T2>
	void push(T2 &&el) {
		auto ind = curIndex & ~(0x80000000);
		auto const flag = curIndex & (0x80000000);
		arr[ ind ] = std::forward<T2>(el);
		
		curIndex = (ind+1 == maxSize) ? 0x80000000 : (ind+1 | flag);
	}
	
	int32_t size() const {
		bool const max{ (curIndex & 0x80000000) != 0 };
		return max ? maxSize : curIndex;
	}
	T &operator[](int32_t index) {
		return arr[index];
	}
};

double heightAt(vec2i const flatChunk, vec2i const block) {
	static siv::PerlinNoise perlin{ (uint32_t)rand() };
	auto const value = perlin.octave2D(
		(flatChunk.x * 1.0 * Chunks::chunkDim + block.x) / 20.0, 
		(flatChunk.y * 1.0 * Chunks::chunkDim + block.y) / 20.0, 
		3
	);
						
	return misc::map<double>(misc::clamp<double>(value,-1,1), -1, 1, 5, 15);
}

vec3i getTreeBlock(vec2i const flatChunk) {
	static auto const random = [](vec2i const co) -> double {
		auto const fract = [](auto it) -> auto { return it - std::floor(it); };
		return fract(sin( vec2d(co).dot(vec2d(12.9898, 78.233)) ) * 43758.5453);
	};
	
	vec2i const it( 
		(vec2d(random(vec2i{flatChunk.x+1, flatChunk.y}), random(vec2i{flatChunk.x*3, flatChunk.y*5})) 
		 * Chunks::chunkDim)
		.floor().clamp(0, 15)
	);
	
	auto const height{ heightAt(flatChunk,it) };
	
	assert(height >= 0 && height < 16);
	return vec3i{ it.x, int32_t(std::floor(height))+1, it.y };
}

void genTrees(vec3i const chunk, Chunks::ChunkData &data, vec3i &start, vec3i &end) {	
	for(int32_t cx{-1}; cx <= 1; cx ++) {
		for(int32_t cz{-1}; cz <= 1; cz ++) {
			vec3i const chunkOffset{ cx, -chunk.y, cz };
			auto const curChunk{ chunk + chunkOffset };
			
			auto const treeBlock{ getTreeBlock(vec2i{curChunk.x, curChunk.z}) };
			
			for(int32_t x{-2}; x <= 2; x++) {
				for(int32_t y{0}; y < 6; y++) {
					for(int32_t z{-2}; z <= 2; z++) {
						vec3i tl{ x,y,z };// tree-local block
						auto const blk{ chunkOffset * Chunks::chunkDim + treeBlock + tl };
						auto const index{ Chunks::blockIndex(blk) };
						if(blk.inMMX(vec3i{0}, vec3i{Chunks::chunkDim}).all() && data[index] == 0) {
							bool is = false;
							if((is = tl.x == 0 && tl.z == 0 && tl.y <= 4)) data[index] = 4;
							else if((is = 
									 (tl.y >= 2 && tl.y <= 3
									  && !( (abs(x) == abs(z))&&(abs(x)==2) )
								     ) || 
								     (tl.in(vec3i{-1, 4, -1}, vec3i{1, 5, 1}).all()
									  && !( (abs(x) == abs(z))&&(abs(x)==1) &&(tl.y==5 || (treeBlock.x*(x+1)/2+treeBlock.z*(z+1)/2)%2==0) )
									 )
							)) data[index] = 5;
							
							if(is) {
								start = start.min(blk);
								end   = end  .max(blk);
							}
						}
					}
				}
			}
		}
	}
}

std::string chunkFilename(Chunks::Chunk const &chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save/" << pos << ".cnk";
	return ss.str();
}

void writeChunk(Chunks::Chunk &chunk) {
	return;
	auto const &data{ chunk.data() };
	
	std::ofstream chunkFileOut{ chunkFilename(chunk), std::ios::binary };
	
	for(int x{}; x < Chunks::chunkDim; x++) 
	for(int y{}; y < Chunks::chunkDim; y++) 
	for(int z{}; z < Chunks::chunkDim; z++) {
		vec3i const blockCoord{x,y,z};
		uint16_t const &block = data[Chunks::blockIndex(blockCoord)];
		
		uint8_t const blk[] = { (unsigned char)((block >> 0) & 0xff), (unsigned char)((block >> 8) & 0xff) };
		chunkFileOut.write(reinterpret_cast<char const *>(&blk[0]), 2);
	}
	
	chunk.modified() = false;
}

void generateChunkData(Chunks::Chunk chunk) {
	auto const &index{ chunk.chunkIndex() };
	auto const &pos{ chunk.position() };
	auto &chunks{ chunk.chunks() };
	auto &data{ chunk.data() };
	auto &aabb{ chunk.aabb() };
	auto &neighbours_{ chunk.neighbours() };
	Chunks::Neighbours neighbours{};
	
	for(int i{}; i < Chunks::Neighbours::neighboursCount; i++) {
		vec3i const offset{ Chunks::Neighbours::indexAsDir(i) };
		if(Chunks::Neighbours::isSelf(i)) { neighbours[i] = Chunks::OptionalNeighbour(index); continue; }

		auto const neighbourPos{ pos + offset };
		auto const neighbourIndexP{ chunks.chunksIndex_position.find(neighbourPos) };
		
		if(neighbourIndexP == chunks.chunksIndex_position.end()) neighbours[i] = Chunks::OptionalNeighbour();
		else {
			int neighbourIndex = neighbourIndexP->second;
			neighbours[i] = Chunks::OptionalNeighbour(neighbourIndex);
			chunks[neighbourIndex].neighbours()[Chunks::Neighbours::mirror(i)] = index;
			chunks[neighbourIndex].gpuPresent() = false;
		}
	}
	
	neighbours_ = neighbours;
	
	//auto const filename{ chunkFilename(chunk) };
			
	//std::ifstream chunkFileIn{ filename, std::ios::binary };
	
	vec3i start{15};
	vec3i end  {0 };
		
	//if(chunkFileIn.fail()) 
	{
		//chunkFileIn.close();
		
		double heights[Chunks::chunkDim * Chunks::chunkDim];
		for(int z = 0; z < Chunks::chunkDim; z++) 
		for(int x = 0; x < Chunks::chunkDim; x++) {
			heights[z* Chunks::chunkDim + x] = heightAt(vec2i{pos.x,pos.z}, vec2i{x,z});
		}

		for(int y = 0; y < Chunks::chunkDim; ++y) 
			for(int z = 0; z < Chunks::chunkDim; ++z)
				for(int x = 0; x < Chunks::chunkDim; ++x) {
					vec3i const blockCoord{ x, y, z };
					//auto const height{ heightAt(vec2i{pos.x,pos.z}, vec2i{x,z}) };
					auto const height{ heights[z * Chunks::chunkDim + x] };
					auto const index{ Chunks::blockIndex(blockCoord) };
					//if(misc::mod(int32_t(height), 9) == misc::mod((pos.y * Chunks::chunkDim + y + 1), 9)) { //repeated floor
					double const diff{ height - double(pos.y * Chunks::chunkDim + y) };
					if(diff >= 0) {
						uint16_t block = 0;
						if(diff < 1) block = 1; //grass
						else if(diff < 5) block = 2; //dirt
						else block = 6; //stone
						data[index] = block;
						
						start = start.min(blockCoord);
						end   = end  .max(blockCoord);
					}
					else {
						data[index] = 0;
					}
				}
			
		genTrees(pos, data, *&start, *&end);
		
		//writeChunk(chunk);
		chunk.modified() = true;
	}
	/*else {
		for(int x{}; x < Chunks::chunkDim; x++) 
		for(int y{}; y < Chunks::chunkDim; y++) 
		for(int z{}; z < Chunks::chunkDim; z++) 
		{
			vec3i const blockCoord{x,y,z};
			//uint16_t &block = data[Chunks::blockIndex(blockCoord)];
			uint8_t blk[2];
			
			chunkFileIn.read( reinterpret_cast<char *>(&blk[0]), 2 );
			
			uint16_t const block( blk[0] | (uint16_t(blk[1]) << 8) );
			
			data[Chunks::blockIndex(blockCoord)] = block;
			if(block != 0) {
				start = start.min(blockCoord);
				end   = end  .max(blockCoord);
			}
		}
		chunk.modified() = false;
	}*/
	
	aabb = Chunks::AABB(start, end);
}

void generateChunkData(int32_t const chunkIndex) {
	generateChunkData( Chunks::Chunk{ chunks, chunkIndex } );
}

static int32_t genChunkAt(vec3i const position) {
	int32_t const usedIndex{ chunks.reserve() };
	auto const chunkIndex{ chunks.used[usedIndex] };
					
	chunks.chunksPos[chunkIndex] = position;
	chunks.chunksIndex_position[position] = chunkIndex;
	chunks.gpuPresent[chunkIndex] = false;
	generateChunkData(chunkIndex);
	
	return usedIndex;
}

static void loadChunks() {
	if(debugBtn1) return;
	static std::vector<int8_t> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.resize(viewWidth*viewWidth*viewWidth);
	for(size_t i = 0; i != chunksPresent.size(); i++)
		chunksPresent[i] = -1;
	
	vec3i playerChunk{ currentCoord().chunk() };
	
	vec3<int32_t> b{viewDistance, viewDistance, viewDistance};
	
	chunks.filterUsed([&](int chunkIndex) -> bool { //keep
			auto const relativeChunkPos = chunks.chunksPos[chunkIndex] - playerChunk;
			if(relativeChunkPos.in(-b, b).all()) {
				auto const index2 = relativeChunkPos + b;
				chunksPresent[index2.x + index2.y * viewWidth  + index2.z * viewWidth * viewWidth] = 1;
				
				return true;
			} 
			return false;
		}, 
		[&](int chunkIndex) -> void { //free chunk
			auto chunk{ chunks[chunkIndex] };
			chunks.chunksIndex_position.erase( chunk.position() );
			if(chunk.modified()) {
				writeChunk(chunk);
			}
			auto const &neighbours{ chunk.neighbours() };
			for(int i{}; i < Chunks::Neighbours::neighboursCount; i++) {
				auto const &optNeighbour{ neighbours[i] };
				if(Chunks::Neighbours::isSelf(i)) continue;
				if(optNeighbour) {
					auto const neighbourIndex{ optNeighbour.get() };
					chunks[neighbourIndex].neighbours()[Chunks::Neighbours::mirror(i)] = Chunks::OptionalNeighbour();
					chunks[neighbourIndex].gpuPresent() = false;
				}
			}
		}
	);

	for(int i = 0; i < viewWidth; i ++) {
		for(int j = 0; j < viewWidth; j ++) {
			for(int k = 0; k < viewWidth; k ++) {
				int index = chunksPresent[i+j*viewWidth+k*viewWidth*viewWidth];
				if(index == -1) {//generate chunk
					vec3<int32_t> const relativeChunkPos{ vec3<int32_t>{i, j, k} - b };
					vec3<int32_t> const chunkPos{ relativeChunkPos + playerChunk };
				
					genChunkAt(chunkPos);
				}
			}
		}
	}
}

struct PosDir {
	vec3l start;
	vec3l end;
	
	vec3i direction;
	vec3i chunk;
	
	PosDir(ChunkCoord const coord, vec3l const line): 
		start{ coord.chunkPart__long() },
		end{ start + line },
		
		direction{ line.sign() },
		chunk{ coord.chunk() }
	{} 
	
	constexpr int64_t atCoord(vec3i const inAxis_, int64_t coord, vec3i const outAxis_) const {
		vec3l const inAxis( inAxis_ );
		vec3l const outAxis( outAxis_ );
		auto const ist = start.dot(inAxis);
		auto const ind = end.dot(inAxis); 
		auto const ost = start.dot(outAxis); 
		auto const ond = end.dot(outAxis); 
		return ist == ind ? ost : ( ost + (ond - ost)*(coord-ist) / (ind - ist) );		
	};
	
	int difference(vec3l const p1, vec3l const p2) const {
		vec3l const diff{p1 - p2};
		return (diff * vec3l(direction)).sign().dot(1);
	};
	
	constexpr vec3l part_at(vec3i inAxis, int64_t const coord) const { 
		return vec3l{
			atCoord(inAxis, coord, vec3i{1,0,0}),
			atCoord(inAxis, coord, vec3i{0,1,0}),
			atCoord(inAxis, coord, vec3i{0,0,1})
		};
	}
	ChunkCoord at(vec3i inAxis, int64_t const coord) const { return ChunkCoord{ chunk, ChunkCoord::Fractional{part_at(inAxis, coord)} }; }
};

struct DDA {
private:
	//const
	PosDir posDir;
	
	//mutable
	vec3l current;
	bool end;
public:
	DDA(PosDir const pd_) : 
	    posDir{ pd_ },
		current{ pd_.start },
		end{ false }
	{}
	
	vec3b next() {
		if(end) return {};

		struct Candidate { vec3l coord; vec3b side; };
		
		vec3l const nextC{ nextCoords(current, posDir.direction) };
		
		Candidate const candidates[] = {
			Candidate{ posDir.part_at(vec3i{1,0,0}, nextC.x), vec3b{1,0,0} },
			Candidate{ posDir.part_at(vec3i{0,1,0}, nextC.y), vec3b{0,1,0} },
			Candidate{ posDir.part_at(vec3i{0,0,1}, nextC.z), vec3b{0,0,1} },
			Candidate{ posDir.end, 0 }//index == 3
		};
		
		int minI{ 3 };
		Candidate minCand{ candidates[minI] };
		
		for(int i{}; i < 4; i++) {
			auto const cand{ candidates[i] };
			auto const diff{ posDir.difference(cand.coord, minCand.coord) };
			if(posDir.difference(cand.coord, current) > 0 && diff <= 0) {
				minI = i;
				if(diff == 0) {
					minCand = Candidate{ 
						minCand.coord * vec3l(!vec3b(cand.side)) + cand.coord * vec3l(cand.side),
						minCand.side || cand.side
					};
				}
				else {
					minCand = cand;
				}
			}
		}
		
		end = minI == 3;
		current = minCand.coord;
		
		return minCand.side;
	}
	
	static constexpr vec3l nextCoords(vec3l const current, vec3i const dir) {
		return current.applied([&](auto const coord, auto const i) -> int64_t {
			//return ((coord / ChunkCoord::fracBlockDim) + std::max(dir[i], 0)) << ChunkCoord::fracBlockDimAsPow2;
			
			if(dir[i] >= 0) //round down
				return ((coord >> ChunkCoord::fracBlockDimAsPow2) + 1) << ChunkCoord::fracBlockDimAsPow2;
			else //round up
				return (-((-coord) >> ChunkCoord::fracBlockDimAsPow2) - 1) << ChunkCoord::fracBlockDimAsPow2;
		});
	};
	
	#define G(name) auto get_##name() const { return name ; }
	G(posDir)
	G(current)
	G(end)
	#undef G
};

static void updateCollision(ChunkCoord &player, vec3d &playerForce, bool &isOnGround) {	
	auto const playerChunk{ player.chunk() };
	
	vec3l playerPos{ player.chunkPart__long() };
	vec3d force{ playerForce };
	
	vec3l maxPlayerPos{};
	
	vec3i dir{};
	vec3b positive_{};
	vec3b negative_{};
	vec3i positive{};
	vec3i negative{};
	
	vec3l playerMin{};
	vec3l playerMax{};
	
	vec3i min{};
	vec3i max{};
	
	auto const updateBounds = [&]() {	
		vec3l const force_ = ChunkCoord::posToFracTrunk(force);
		
		dir = force_.sign();
		positive_ = dir > vec3i(0);
		negative_ = dir < vec3i(0);
		positive = vec3i(positive_);
		negative = vec3i(negative_);
		
		maxPlayerPos = playerPos + force_;
		
		/*static_*/assert(width_i % 2 == 0);
		playerMin = vec3l{ playerPos.x-width_i/2, playerPos.y         , playerPos.z-width_i/2 };
		playerMax = vec3l{ playerPos.x+width_i/2, playerPos.y+height_i, playerPos.z+width_i/2 };
		
		min = (playerPos - vec3l{width_i/2,0,width_i/2}).applied([](auto const coord, auto i) -> int32_t {
			return misc::divFloor(coord, ChunkCoord::fracBlockDim);
		});
		max = (playerPos + vec3l{width_i/2,height_i,width_i/2}).applied([](auto const coord, auto i) -> int32_t {
			return misc::divCeil(coord, ChunkCoord::fracBlockDim)-1;
		});
	};
	updateBounds();
	
	Chunks::Move_to_neighbour_Chunk chunk{ chunks, playerChunk };
	
	if(dir.y != 0){
		auto const startY{ misc::divFloor(positive_.y ? playerMax.y : playerMin.y, ChunkCoord::fracBlockDim)-negative.y };
		auto const endY{ misc::divFloor(maxPlayerPos.y + (positive_.y ? height_i : 0), ChunkCoord::fracBlockDim) };
		auto const yCount{ (endY - startY) * dir.y };
		
		bool is = false;
		int64_t minY = maxPlayerPos.y;
		
		for(auto x{ min.x }; x <= max.x; x++)
		for(auto z{ min.z }; z <= max.z; z++)
			for(int32_t yo{}; yo <= yCount; yo++) {
				vec3l const blockPos{x, startY + yo * dir.y, z};
				
				ChunkCoord const coord{ //TODO: remove conversion from block to frac
					playerChunk,
					ChunkCoord::Block{ vec3i(blockPos) } 
				};
						
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				auto const chunkIndex{ chunk.move(blockChunk, 0).get() };
				if(chunkIndex == -1) { std::cout << "collision y: add chunk gen!" << coord << '\n'; return; }
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					auto const newY{ ChunkCoord::blockToFrac(vec3i(blockPos.y+negative.y)).x - (positive_.y ? height_i : 0)}; 
					if(positive_.y ? (newY >= playerPos.y && newY <= minY) : (newY <= playerPos.y && newY >= minY)) {
						is = true;
						minY = newY;
						break;
					}
				}
			}
		
		playerPos.y = minY;
		
		if(is) {
			if(negative_.y) isOnGround = true;
			force *= 0.8;
			force.y = 0;
		}
		
		updateBounds();
	}

	if(dir.x != 0){
		auto const start{ misc::divFloor(positive_.x ? playerMax.x : playerMin.x, ChunkCoord::fracBlockDim)-negative.x };
		auto const end{ misc::divFloor(maxPlayerPos.x+width_i/2*dir.x, ChunkCoord::fracBlockDim) };
		auto const count{ (end - start) * dir.x };
		
		bool is = false;
		int64_t minX = maxPlayerPos.x;
		
		for(auto y{ min.y }; y <= max.y; y++)
		for(auto z{ min.z }; z <= max.z; z++)
			for(int32_t xo{}; xo <= count; xo++) {
				vec3l const blockPos{start + xo * dir.x, y, z};
				
				ChunkCoord const coord{ //TODO: remove conversion from block to frac
					playerChunk,
					ChunkCoord::Block{ vec3i(blockPos) } 
				};
						
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				auto const chunkIndex{ chunk.move(blockChunk, 1).get() };
				if(chunkIndex == -1) { std::cout << "collision x: add chunk gen!" << coord << '\n'; return; }
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					auto const newX{ ChunkCoord::blockToFrac(vec3i(blockPos.x + negative.x)).x - width_i/2*dir.x }; 
					if(positive_.x ? (newX >= playerPos.x && newX <= minX) : (newX <= playerPos.x && newX >= minX)) {
						minX = newX;
						is = true;
						break;
					}
				}
			}
		
		playerPos.x = minX;
		if(is) {
			force.x = 0;
		}
		updateBounds();
	}
	
	if(dir.z != 0){
		auto const start{ misc::divFloor(positive_.z ? playerMax.z : playerMin.z, ChunkCoord::fracBlockDim)-negative.z };
		auto const end{ misc::divFloor(maxPlayerPos.z+width_i/2*dir.z, ChunkCoord::fracBlockDim) };
		auto const count{ (end - start) * dir.z };
		
		bool is = false;
		int64_t minZ = maxPlayerPos.z;
		
		for(auto x{ min.x }; x <= max.x; x++)
		for(auto y{ min.y }; y <= max.y; y++)
			for(int32_t zo{}; zo <= count; zo++) {
				vec3l const blockPos{x, y, start + zo * dir.z};
				
				ChunkCoord const coord{ //TODO: remove conversion from block to frac
					playerChunk,
					ChunkCoord::Block{ vec3i(blockPos) } 
				};
						
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				auto const chunkIndex{ chunk.move(blockChunk, 2).get() };
				if(chunkIndex == -1) { std::cout << "collision z: add chunk gen!" << coord << '\n'; return; }
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					auto const newZ{ ChunkCoord::blockToFrac(vec3i(blockPos.z + negative.z)).x - width_i/2*dir.z }; 
					if(positive_.z ? (newZ >= playerPos.z && newZ <= minZ) : (newZ <= playerPos.z && newZ >= minZ)) {
						minZ = newZ;
						is = true;
						break;
					}
				}
			}
		
		playerPos.z = minZ;
		if(is) {
			force.z = 0;			
		}
		updateBounds();
	}
	
	
	player = ChunkCoord{ playerChunk, ChunkCoord::Fractional{playerPos} };
	playerForce = force * (isOnGround ? vec3d{0.8,1,0.8} : vec3d{1});
}

bool checkCanPlaceBlock(vec3i const blockChunk, vec3i const blockCoord) {
	ChunkCoord const relativeBlockCoord{ ChunkCoord{ blockChunk, ChunkCoord::Block{blockCoord} } - currentCoord() };
	vec3l const blockStartF{ relativeBlockCoord.position__long() };
	vec3l const blockEndF{ blockStartF + ChunkCoord::fracBlockDim };
	
	/*static_*/assert(width_i % 2 == 0);
	
	return !(
		misc::intersectsX(0ll       , height_i ,  blockStartF.y, blockEndF.y) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStartF.x, blockEndF.x) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStartF.z, blockEndF.z)
	);
}

static void update() {	
	static std::chrono::time_point<std::chrono::steady_clock> lastPhysicsUpdate{std::chrono::steady_clock::now()};
	static std::chrono::time_point<std::chrono::steady_clock> lastBlockUpdate{std::chrono::steady_clock::now()};
	auto const now{std::chrono::steady_clock::now()};

	auto const diffBlockMs{ std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlockUpdate).count() };
	auto const diffPhysicsMs{ std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPhysicsUpdate).count() };
	
	
	for(size_t i{}; i < sizeof(keys)/sizeof(keys[0]); ++i) {
		auto &key{ keys[i] };
		auto const action{ misc::to_underlying(key) };
		if(action == GLFW_PRESS) {
			key = Key::REPEAT;
		}
		else if(action == GLFW_REPEAT) {
			handleKey(i);
		}
		else if(action == GLFW_RELEASE) {
			key = Key::NOT_PRESSED;
		}
	}

   // vec2<double> diff = playerInput.panning;// (mousePos - pmousePos) / windowSize_d;
	
	if(diffBlockMs >= blockActionCD * 1000 && blockAction != BlockAction::NONE && !isSpectator) {
		bool isAction = false;

		ChunkCoord const viewport{ currentCoord() + ChunkCoord::Fractional{ChunkCoord::posToFracTrunk(viewportOffset_)} };
		PosDir const pd{ PosDir(viewport, ChunkCoord::posToFracTrunk(viewport_current().forwardDir() * 7)) };
		vec3i const dirSign{ pd.direction };
		DDA checkBlock{ pd };
		
		Chunks::Move_to_neighbour_Chunk chunk{ chunks, pd.chunk };
	
		if(blockAction == BlockAction::BREAK) {
			for(int i = 0;; i++) {
				vec3b const intersectionAxis{ checkBlock.next() };
				vec3l const intersection{ checkBlock.get_current() };
				
				if(intersectionAxis == 0) break;
				
				ChunkCoord const coord{ 
					pd.chunk,
					ChunkCoord::Block{ 
						  ChunkCoord::fracToBlock(intersection)
						+ pd.direction.min(0) * vec3i(intersectionAxis)
					} 
				};
				
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				int chunkIndex{ chunk.move(blockChunk, 10).get() };
				
				if(chunkIndex == -1) { 
					//auto const usedIndex{ genChunkAt(blockChunk) }; //generates every frame
					//chunkIndex = chunks.usedChunks()[usedIndex];
					std::cout << "block br: add chunk gen!" << coord << '\n'; 
					break;
				}
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					auto &chunkData{ chunks.chunksData[chunkIndex] };
					auto const index{ Chunks::blockIndex(blockCoord) };
					uint16_t &block{ chunkData[index] };
					block = 0;
					chunks.gpuPresent[chunkIndex] = false;
					chunks.modified[chunkIndex] = true;
					isAction = true;
					
					auto &aabb{ chunks.chunksAABB[chunkIndex] };
					vec3i const start_{ aabb.start() };
					vec3i const end_  { aabb.end  () };
					
					vec3i start{ 16 };
					vec3i end  { 0 };
					for(int32_t x = start_.x; x <= end_.x; x++)
					for(int32_t y = start_.y; y <= end_.y; y++)
					for(int32_t z = start_.z; z <= end_.z; z++) {
						vec3i const blk{x, y, z};
						if(chunkData[Chunks::blockIndex(blk)] != 0) {
							start = start.min(blk);
							end   = end  .max(blk);
						}
					}
					
					aabb = Chunks::AABB(start, end);
					break;
				}
				else if(checkBlock.get_end() || i >= 100) break;
			}
		}
		else {
			for(int i = 0;; i++) {
				vec3b const intersectionAxis{ checkBlock.next() };
				vec3l const intersection{ checkBlock.get_current() };
				
				if(intersectionAxis == 0) break;
				
				ChunkCoord const coord{ 
					pd.chunk,
					ChunkCoord::Block{ 
						  ChunkCoord::fracToBlock(intersection)
						+ pd.direction.min(0) * vec3i{intersectionAxis}
					} 
				};
				
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				int chunkIndex{ chunk.move(blockChunk, 15).get() }; 
				
				if(chunkIndex == -1) { 
					//auto const usedIndex{ genChunkAt(blockChunk) }; //generates every frame
					//chunkIndex = chunks.usedChunks()[usedIndex];
					std::cout << "block pl: add chunk gen!" << coord << '\n'; 
					break;
				}
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					ChunkCoord const bc{ coord - ChunkCoord::Block{ dirSign * vec3i(intersectionAxis) } };
					vec3i const blockCoord = bc.blockInChunk();
					vec3i const blockChunk = bc.chunk();
			
					int chunkIndex{ chunk.move(blockChunk, 5).get() };
				
					auto const index{ Chunks::blockIndex(blockCoord) };
					uint16_t &block{ chunks.chunksData[chunkIndex][index] };
					
					if(checkCanPlaceBlock(blockChunk, blockCoord) && block != 0) { std::cout << "!\n"; } 
					if(checkCanPlaceBlock(blockChunk, blockCoord) && block == 0) {
						block = blockId;//1;
						chunks.gpuPresent[chunkIndex] = false;
						chunks.modified[chunkIndex] = true;
						isAction = true;
						
						auto &aabb{ chunks.chunksAABB[chunkIndex] };
						vec3i start{ aabb.start() };
						vec3i end  { aabb.end  () };
						
						start = start.min(blockCoord);
						end   = end  .max(blockCoord);
					
						aabb = Chunks::AABB(start, end);
					}
					break;
				}
				else if(checkBlock.get_end() || i >= 100) break;
			}
		}
	
		if(isAction) {
			lastBlockUpdate = now;
		}
	}
	
	{
		double projection[3][3];
		spectatorViewport.localToGlobalSpace(&projection);
				
		auto const movement{ 
			vecMult( projection, vec3d(spectatorInput.movement).normalizedNonan() ) 
			* spectatorSpeed
			* (shift ? 1.0*speedModifier : 1)
			* (ctrl  ? 1.0/speedModifier : 1)
		};
		
		spectatorCoord += movement;
		spectatorViewport.rotation += spectatorInput.panning * (2 * misc::pi) * vec2d{ 0.8, -0.8 };
		spectatorViewport.rotation.y = misc::clamp(spectatorViewport.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
		spectatorInput = Input();
	}
	
	{
		playerViewport.rotation += playerInput.panning * (2 * misc::pi) * vec2d{ 0.8, -0.8 };
		playerViewport.rotation.y = misc::clamp(playerViewport.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
		
		if(diffPhysicsMs > fixedDeltaTime * 1000) {
			lastPhysicsUpdate += std::chrono::milliseconds(static_cast<long long>(fixedDeltaTime*1000.0));
			
			static vec3d currentPlayerMovement{};
			
			auto const playerMovement{ 
				(
					  playerViewport.flatForwardDir()*playerInput.movement.z
					+ playerViewport.flatTopDir()    *playerInput.movement.y
					+ playerViewport.flatRightDir()  *playerInput.movement.x
				).normalizedNonan()
			    * playerSpeed
				* (shift ? 1.0*speedModifier : 1)
				* (ctrl  ? 1.0/speedModifier : 1)
			}; 
		
			if(!debugBtn0) {
				playerForce += vec3d{0,-1,0} * fixedDeltaTime; 
				if(isOnGround) {
					playerForce += (
						vec3d{0,1,0}*14*double(playerInput.jump)
						+ playerMovement	
					) * fixedDeltaTime;
				}
				else {
					auto const movement{ playerMovement * 0.5 * fixedDeltaTime };
					playerForce = playerForce.applied([&](double const coord, auto const index) -> double { 
						return misc::clamp(coord + movement[index], fmin(coord, movement[index]), fmax(coord, movement[index]));
					});
				}

				isOnGround = false;
				updateCollision(playerCoord, playerForce, isOnGround);
			}
		}
		
		playerInput = Input();
	}
	
	loadChunks();
}
	
int main(void) {	
    if (!glfwInit()) return -1;
	
	loadChunks();

    GLFWmonitor* monitor;
#ifdef FULLSCREEN
    monitor = glfwGetPrimaryMonitor();
#else
    monitor = NULL;
#endif // !FULLSCREEN

    window = glfwCreateWindow(windowSize.x, windowSize.y, "VMC", monitor, NULL);

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
	if(mouseCentered) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
        glfwTerminate();
        return -1;
    }
	
	glEnable(GL_DEPTH_TEST);  
	glDepthFunc(GL_LESS); 
	
	glEnable(GL_CULL_FACE); 
	glCullFace(GL_BACK); 
	//glEnable(GL_FRAMEBUFFER_SRGB); 
	
    fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));

    //callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
	glfwSetCursorPos(window, 0, 0);
	cursor_position_callback(window, 0, 0);
	
	//load shaders
	reloadShaders();
	
	
	GLuint fontVB;
	glGenBuffers(1, &fontVB);
	glBindBuffer(GL_ARRAY_BUFFER, fontVB);
	glBufferData(GL_ARRAY_BUFFER, sizeof(std::array<std::array<vec2f, 4>, 15>{}), NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	GLuint fontVA;
	glGenVertexArrays(1, &fontVA);
	glBindVertexArray(fontVA);
		glBindBuffer(GL_ARRAY_BUFFER, fontVB);
		
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
	
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), NULL); //startPos
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)( 2*sizeof(float) )); //endPos
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)( 4*sizeof(float) )); //startUV
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)( 6*sizeof(float) )); //endUV
	
		glVertexAttribDivisor(0, 1);
		glVertexAttribDivisor(1, 1);
		glVertexAttribDivisor(2, 1);
		glVertexAttribDivisor(3, 1);
	glBindVertexArray(0); 
	
	
	GLuint testVB;
	glGenBuffers(1, &testVB);
	
	GLuint testVA;
	glGenVertexArrays(1, &testVA);
	glBindVertexArray(testVA);
		glBindBuffer(GL_ARRAY_BUFFER, testVB);
		
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
	
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), NULL); //relativePos
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)( 3*sizeof(float) )); //relativePos
		
		
	glBindVertexArray(0); 
	
	
	while ((err = glGetError()) != GL_NO_ERROR)
    {
        std::cout << err << std::endl;
    }

    auto start = std::chrono::steady_clock::now();
	
	MeanCounter<150> mc{};
	int _i_ = 50;

    while (!glfwWindowShouldClose(window))
    {
		auto startFrame = std::chrono::steady_clock::now();
		
		//auto &player{ playerCoord() };
		auto &currentViewport{ viewport_current() };
		//auto const position{ player.position()+viewportOffset() };
        auto const rightDir{ currentViewport.rightDir() };
        auto const topDir{ currentViewport.topDir() };
        auto const forwardDir{ currentViewport.forwardDir() };
		
		float toLoc[3][3];
		float toGlob[3][3];
		currentViewport.localToGlobalSpace(&toGlob);
		currentViewport.globalToLocalSpace(&toLoc);
		ChunkCoord const cameraCoord{ currentCoord() + viewportOffset() };
		auto const cameraChunk{ cameraCoord.chunk() };
		auto const cameraPosInChunk{ cameraCoord.positionInChunk() };
		
		float const toLoc4[4][4] = {
			{ toLoc[0][0], toLoc[0][1], toLoc[0][2], 0 },
			{ toLoc[1][0], toLoc[1][1], toLoc[1][2], 0 },
			{ toLoc[2][0], toLoc[2][1], toLoc[2][2], 0 },
			{ 0          , 0          , 0          , 1 },
		};
		
		//glClear(GL_COLOR_BUFFER_BIT);
		
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glEnable(GL_DEPTH_TEST); 
		glClear(GL_DEPTH_BUFFER_BIT);
		
		if(isSpectator){
			auto const playerRelativePos{ vec3f((playerCoord - currentCoord()).position()) };
			float const translation4[4][4] = {
					{ 1, 0, 0, playerRelativePos.x },
					{ 0, 1, 0, playerRelativePos.y },
					{ 0, 0, 1, playerRelativePos.z },
					{ 0, 0, 0, 1                   },
			};
			float const model[4][4] ={
				{ (float)width, 0, 0, (float)-width/2 },
				{ 0, (float)height, 0, (float)0 },
				{ 0, 0, (float)width, (float)-width/2 },
				{ 0, 0, 0, 1  },
			};
						
			float posToScale[4][4];
			float playerToLocal[4][4];
			
			
			misc::matMult(translation4, model, &posToScale);
			misc::matMult(toLoc4, posToScale, &playerToLocal);
			
			glUseProgram(playerProgram);
			
			glUniformMatrix4fv(pl_modelMatrix_u, 1, GL_TRUE, &playerToLocal[0][0]);
						
			glDrawArrays(GL_TRIANGLES, 0,36);
		}
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glEnable(GL_FRAMEBUFFER_SRGB); 
		glDisable(GL_DEPTH_TEST); 
		
		
		glUseProgram(mainProgram);
		
        glUniform3f(rightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(topDir_u, topDir.x, topDir.y, topDir.z);

        glUniform1f(mouseX_u, mousePos.x / windowSize_d.x);
        glUniform1f(mouseY_u, mousePos.y / windowSize_d.y);

        glUniform1f(time_u,
            sin(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0
            )
        );
		
		if(testInfo) {
			std::cout.precision(17);
			std::cout << currentCoord().positionInChunk() << '\n';
			std::cout.precision(2);
			std::cout << currentCoord().blockInChunk() << '\n';
			std::cout << currentCoord().chunk() << '\n';
			std::cout << "----------------------------\n";
		}
		
		glUniformMatrix4fv(toLocal_matrix_u, 1, GL_TRUE, &toLoc4[0][0]);
		glProgramUniformMatrix4fv(debugProgram, db_toLocal_u, 1, GL_TRUE, &toLoc4[0][0]);
		
		glUniform3i(playerChunk_u, cameraChunk.x, cameraChunk.y, cameraChunk.z);
		glUniform3f(playerInChunk_u, cameraPosInChunk.x, cameraPosInChunk.y, cameraPosInChunk.z);
		
		glProgramUniform3i(debugProgram, db_playerChunk_u, cameraChunk.x, cameraChunk.y, cameraChunk.z);
		glProgramUniform3f(debugProgram, db_playerInChunk_u, cameraPosInChunk.x, cameraPosInChunk.y, cameraPosInChunk.z);
		
		Chunks::Move_to_neighbour_Chunk const playerChunkCand{ chunks, cameraChunk };
		if(playerChunkCand.is()) {
			int playerChunkIndex = playerChunkCand.get().chunkIndex();
			glUniform1i(startChunkIndex_u, playerChunkIndex);
			
			int i = 0;
			for(auto const chunkIndex : chunks.used) {
				auto &chunkData{ chunks.chunksData[chunkIndex] };
				std::vector<bool>::reference gpuPresent = chunks.gpuPresent[chunkIndex];
				
				if(!gpuPresent) {
					if(chunkIndex >= gpuChunksCount) {
						std::cout << "Error: gpu buffer was not properly resized. size=" << gpuChunksCount << " expected=" << (chunkIndex+1);
						exit(-1);
					}
					uint32_t const aabbData{ chunks[chunkIndex].aabb().getData() };
					vec3i const chunkPosition{ chunks[chunkIndex].position() };
					auto const &neighbours{ chunks[chunkIndex].neighbours() };
					
					gpuPresent = true;
					
					static_assert(sizeof chunkData == (8192));
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkIndex_u); 
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8192 * chunkIndex, 8192, &chunkData);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
					
					
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksPostions_ssbo); 
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(vec3i) * chunkIndex, sizeof(vec3i), &chunkPosition);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
					
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo); 
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * chunkIndex, sizeof(uint32_t), &aabbData);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
					
					static_assert(sizeof(neighbours) == sizeof(int32_t) * 6);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo); 
					glBufferSubData(
						GL_SHADER_STORAGE_BUFFER, 
						sizeof(int32_t) * Chunks::Neighbours::neighboursCount * chunkIndex, 
						Chunks::Neighbours::neighboursCount * sizeof(uint32_t), 
						&neighbours
					);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				} 
			}

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		
		glDisable(GL_FRAMEBUFFER_SRGB); 
		
		/*{
			glUseProgram(testProgram);
			glUniformMatrix4fv(tt_toLocal_u, 1, GL_TRUE, &toLoc4[0][0]);
			
			glBindBuffer(GL_ARRAY_BUFFER, testVB);
			glBufferData(GL_ARRAY_BUFFER, size * 2*sizeof(vec3f), &[0], GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			
			glPointSize(10);
			glBindVertexArray(testVA);
			glDrawArrays(GL_POINTS, 0, relativePositions.size());
			glBindVertexArray(0);
		}*/
	
		{ 
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			
			vec2f const textPos(10, 10);
			    
			std::stringstream ss{};
			ss.precision(1);
			ss << std::fixed << (1000000.0 / mc.mean()) << '(' << (1000000.0 / mc.max()) << ')' << "FPS";
			std::string const text{ ss.str() };
	
			auto const textCount{ std::min(text.size(), 15ull) };
			
			std::array<std::array<vec2f, 4>, 15> data;
			
			vec2f currentPoint( textPos.x / windowSize_d.x * 2 - 1, 1 - textPos.y / windowSize_d.y * 2 );
			
			auto const lineH = font.base();
			float const scale = 5;
			
			for(uint64_t i{}; i != textCount; i++) {
				auto const &fc{ font[text[i]] };
				
				data[i] = {
					//pos
					currentPoint + vec2f(0, 0 - lineH) / scale,
					currentPoint + vec2f(fc.width*aspect, fc.height - lineH) / scale, 
					//uv
					vec2f{fc.x, 1-fc.y-fc.height},
					vec2f{fc.x+fc.width,1-fc.y},
				};
				
				currentPoint += vec2f(fc.xAdvance*aspect, 0) / scale;
			}

			glUseProgram(fontProgram);
			
			glBindBuffer(GL_ARRAY_BUFFER, fontVB);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data[0]) * textCount, &data[0]);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			
			glBindVertexArray(fontVA);
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE); 
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, textCount);
				glDepthMask(GL_TRUE);
				glEnable(GL_DEPTH_TEST);
			glBindVertexArray(0);
			
			glDisable(GL_BLEND);
		}
		
		//if(testInfo) std::cout << "FPS:" << (1000000.0 / mc.mean()) << '\n';
		
		testInfo = false; 
		glfwSwapBuffers(window);
		glfwPollEvents();
		
		static GLenum lastError;
        while ((err = glGetError()) != GL_NO_ERROR) {
			if(lastError == err) {
				
			}
            else { 
				std::cout << "glError: " << err << std::endl;
				lastError = err;
			}
		}

        update();
		
		mc.add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startFrame).count());
    }

    glfwTerminate();
	
    return 0;
}

static void quit() {
	for(auto const chunkIndex : chunks.used) {
		auto chunk{ chunks[chunkIndex] };
		
		if(chunk.modified()) {
			writeChunk(chunk);
		}
	}
	
	glfwSetWindowShouldClose(window, GL_TRUE);
}
