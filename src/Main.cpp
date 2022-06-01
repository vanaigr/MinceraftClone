#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop

#include <GLFW/glfw3.h>

#include"Units.h"
#include"Position.h"

#include"Vector.h"
#include"Chunk.h"
#include"Position.h"
#include"Viewport.h"
#include"Lighting.h"

#include"Font.h"
#include"ShaderLoader.h"
#include"Read.h"
#include"Misc.h"
#include"PerlinNoise.h"
#include"Counter.h"

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
static const vec2<double> windowSize_d{ windowSize.convertedTo<double>() };
static double const aspect{ windowSize_d.y / windowSize_d.x };

static constexpr bool loadChunks = false, saveChunks = false;

static double       deltaTime{ 16.0/1000.0 };
static double const fixedDeltaTime{ 16.0/1000.0 };

GLFWwindow* window;

static int viewDistance = 2;


static double const playerHeight{ 1.95 };
static double const playerWidth{ 0.6 };

static int64_t const height_i{ units::posToFracRAway(playerHeight).value() };
static int64_t const width_i{ units::posToFracRAway(playerWidth).value() }; 

static double speedModifier = 2.5;
static double playerSpeed{ 2.7 };
static double spectatorSpeed{ 0.03 };

static vec3d playerForce{};
static bool isOnGround{false};

static vec3l const playerCameraOffset{ 0, units::posToFrac(playerHeight*0.85).value(), 0 };
static pos::Fractional playerCoord{ pos::posToFrac(vec3d{0.01,12.001,0.01}) };
static pos::Fractional playerCamPos{playerCoord + playerCameraOffset}; //position is lerp'ed

static pos::Fractional spectatorCoord{ playerCoord };

static Camera playerCamera{
	aspect,
	90.0 / 180.0 * misc::pi,
	0.001,
	800
};


static Viewport viewportCurrent{ /*angles=*/vec2d{ misc::pi / 2.0, 0 } };
static Viewport viewportDesired{ viewportCurrent };

static bool isSpectator{ false };

static bool isSmoothCamera{ false };
static double zoom = 3;

static double currentZoom() {
	if(isSmoothCamera) return zoom;
	return 1;
}

static Camera &currentCamera() {
	return playerCamera;
}

static pos::Fractional &currentCoord() {
	if(isSpectator) return spectatorCoord;
	return playerCoord;
}

static pos::Fractional currentCameraPos() {
	if(isSpectator) return spectatorCoord;
	return playerCamPos;
}


struct Input {
	vec3i movement;
	bool jump;
};

static Input playerInput, spectatorInput;
static vec2d deltaRotation{ 0 };

Input &currentInput() {
	if(isSpectator) return spectatorInput;
	else return playerInput;
}


static bool testInfo = false, debugInfo = false;
static bool numpad[10]; //[0] - turn off physics. [1] - load new chunks. [2] - update time uniform in main prog
static bool debug{ false };


bool mouseCentered = true;

enum class BlockAction {
	NONE = 0,
	PLACE,
	BREAK
};
static BlockAction blockAction{ BlockAction::NONE };
static double const blockActionCD{ 300.0 / 1000.0 };
static bool breakFullBlock{ false };


enum class Key : uint8_t { RELEASE = GLFW_RELEASE, PRESS = GLFW_PRESS, REPEAT = GLFW_REPEAT, NOT_PRESSED };
static_assert(GLFW_RELEASE >= 0 && GLFW_RELEASE < 256 && GLFW_PRESS >= 0 && GLFW_PRESS < 256 && GLFW_REPEAT >= 0 && GLFW_REPEAT < 256);

static bool shift{ false }, ctrl{ false };
static Key keys[GLFW_KEY_LAST+1];

static void reloadShaders();

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
		glfwSetWindowShouldClose(window, GL_TRUE);
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
	
	if(key == GLFW_KEY_Z && !isPress) {
		breakFullBlock = !breakFullBlock;
	}
	
	if(key == GLFW_KEY_MINUS && isPress) zoom = 1 + (zoom - 1) * 0.95;
	else if(key == GLFW_KEY_EQUAL/*+*/ && isPress) zoom = 1 + (zoom - 1) * (1/0.95);
		
	if(key == GLFW_KEY_F2 && !isPress) debugInfo = !debugInfo;
	
	if(GLFW_KEY_KP_0 <= key && key <= GLFW_KEY_KP_9  &&  !isPress) numpad[key - GLFW_KEY_KP_0] = !numpad[key - GLFW_KEY_KP_0];

	if(key == GLFW_KEY_D)
		currentInput().movement.x = 1 * isPress;
	else if(key == GLFW_KEY_A)
		currentInput().movement.x = -1 * isPress;
	
	if(key == GLFW_KEY_F5 && action == GLFW_PRESS)
		reloadShaders();
	else if(key == GLFW_KEY_F4 && action == GLFW_PRESS)
		debug = !debug;
	else if(key == GLFW_KEY_F3 && action == GLFW_RELEASE) { 
		if(!isSpectator) {
			spectatorCoord = currentCameraPos();
		}
		isSpectator = !isSpectator;
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
    static vec2<double> relativeTo{ 0, 0 };
	static vec2d pmousePos_{0};
    vec2<double> mousePos_{ mousex,  mousey };
    
	if(mouseCentered) {
		relativeTo += mousePos_ - pmousePos_;
	} else {
		relativeTo += mousePos_;
		mousePos_.x = misc::modf(mousePos_.x, windowSize_d.x);
		mousePos_.y = misc::modf(mousePos_.y, windowSize_d.y);
		relativeTo -= mousePos_;
		glfwSetCursorPos(window, mousePos_.x, mousePos_.y);
	}
	pmousePos_ = mousePos_;
	
	static vec2<double> pmousePos(0, 0);
    vec2<double> const mousePos = vec2<double>(relativeTo.x + mousePos_.x, relativeTo.y + mousePos_.y);
	
	if( mouseCentered) {
		deltaRotation += (mousePos - pmousePos) / windowSize_d;
	}
	
	pmousePos = mousePos;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if(button == GLFW_MOUSE_BUTTON_LEFT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::BREAK : BlockAction::NONE;
	}
	else if(button == GLFW_MOUSE_BUTTON_RIGHT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::PLACE : BlockAction::NONE;
	}
	else if(button == GLFW_MOUSE_BUTTON_MIDDLE) {
		isSmoothCamera = action != GLFW_RELEASE;
		if(!isSmoothCamera) {
			viewportDesired.rotation = viewportCurrent.rotation; //may reset changes from cursor_position_callback
		}
	}
}

static int blockPlaceId = 1;
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	blockPlaceId = 1+misc::mod(blockPlaceId-1 + int(yoffset), 14);
}


static const Font font{ "./assets/font.txt" };


enum Textures : GLuint {
	atlas_it = 0,
	font_it,
	noise_it,
	
	texturesCount
};
static GLuint textures[texturesCount];
static GLuint const		&atlas_t = textures[atlas_it], 
						&font_t  = textures[font_it], 
						&noise_t = textures[noise_it];

enum SSBOs : GLuint {
	chunksBlocks_b = 0,
	chunksPostions_b,
	chunksBounds_b,
	chunksNeighbours_b,
	chunksAO_b,
	chunksSkyLighting_b,
	chunksBlockLighting_b,
	chunksEmittersGPU_b,
	atlasDescription_b,
	
	ssbosCount
};

static GLuint ssbos[ssbosCount];
static GLuint const		&chunksBlocks_ssbo        = ssbos[chunksBlocks_b], 
						&chunksPostions_ssbo      = ssbos[chunksPostions_b], 
						&chunksBounds_ssbo        = ssbos[chunksBounds_b], 
						&chunksNeighbours_ssbo    = ssbos[chunksNeighbours_b],
						&chunksAO_ssbo            = ssbos[chunksAO_b],
						&chunksSkyLighting_ssbo   = ssbos[chunksSkyLighting_b],
						&chunksBlockLighting_ssbo = ssbos[chunksBlockLighting_b],
						&chunksEmittersGPU_ssbo   = ssbos[chunksEmittersGPU_b],
						&atlasDescription_ssbo    = ssbos[atlasDescription_b];

static GLuint mainProgram = 0;
  static GLuint rightDir_u;
  static GLuint topDir_u;
  static GLuint near_u, far_u;
  static GLuint playerRelativePosition_u, drawPlayer_u;
  static GLuint startChunkIndex_u;
  static GLuint time_u;
  static GLuint projection_u, toLocal_matrix_u;
  static GLuint playerChunk_u, playerInChunk_u;

static GLuint fontProgram;

static GLuint testProgram;
	static GLuint tt_projection_u, tt_toLocal_u;
	
static GLuint currentBlockProgram;
  static GLuint cb_blockIndex_u;

static GLuint blockHitbox_p;
  static GLuint blockHitboxProjection_u, blockHitboxModelMatrix_u;

chunk::Chunks chunks{};

void resizeGPUBuffers() {
	auto const gpuChunksCount{ chunks.usedChunks().size() };
	auto &it = chunks.chunksStatus;
	
	for(auto &status : it) {
		status.resetStatus();
		status.setBlocksUpdated(true);
		status.setLightingUpdated(true);
		status.setNeighbouringEmittersUpdated(true);
	}
	
	
	static_assert(sizeof(chunk::ChunkData{}) == 16384);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlocks_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkData{}), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksBlocks_b, chunksBlocks_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksPostions_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(vec3i), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksPostions_b, chunksPostions_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksBounds_b, chunksBounds_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * chunk::Neighbours::neighboursCount * sizeof(int32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_b, chunksNeighbours_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	static_assert(sizeof(chunk::ChunkAO) == sizeof(uint8_t) * chunk::ChunkAO::size);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksAO_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkAO), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksAO_b, chunksAO_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	static_assert(sizeof(chunk::ChunkLighting) == sizeof(uint8_t) * chunk::ChunkLighting::size);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksSkyLighting_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkLighting), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksSkyLighting_b, chunksSkyLighting_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);		
	
	static_assert(sizeof(chunk::ChunkLighting) == sizeof(uint8_t) * chunk::ChunkLighting::size);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlockLighting_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkLighting), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksBlockLighting_b, chunksBlockLighting_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	static_assert(sizeof(chunk::Chunk3x3BlocksList) == sizeof(uint32_t) * 16);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::Chunk3x3BlocksList), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_b, chunksEmittersGPU_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void printLinkErrors(GLuint const prog, char const *const name) {
	GLint progErrorStatus;
	glGetProgramiv(prog, GL_LINK_STATUS, &progErrorStatus);
	if(!progErrorStatus) return;
	
	std::cout << "Program status: " << progErrorStatus << '\n';
	int length;
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
	GLchar *msg = new GLchar[length + 1];
	glGetProgramInfoLog(prog, length, &length, msg);
	std::cout << "Program error:\n" << msg;
	delete[] msg;
} 

static void reloadShaders() {	
	/*{
		//color
		glActiveTexture(GL_TEXTURE0 + framebufferColor_it);
		glBindTexture(GL_TEXTURE_2D, framebufferColor_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, windowSize.x, windowSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		//depth
		glActiveTexture(GL_TEXTURE0 + framebufferDepth_it);
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
	}*/
	
	glDeleteTextures(texturesCount, &textures[0]);
	glGenTextures   (texturesCount, &textures[0]);
	
	{//ssbos
		glDeleteBuffers(ssbosCount, &ssbos[0]);
		glGenBuffers   (ssbosCount, &ssbos[0]);
		
		{ //atlas desctiption
			auto const c = [](int16_t const x, int16_t const y) -> int32_t {
				return int32_t( uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16) );
			}; //pack coord
			int32_t const sides[] = { //side, top, bottom, alpha. Texture offset in tiles from top-left corner of the atlas
				c(0, 0), c(0, 0), c(0, 0), c(0, 1), //0
				c(1, 0), c(2, 0), c(3, 0), c(0, 1), //grass
				c(3, 0), c(3, 0), c(3, 0), c(0, 1), //dirt
				c(4, 0), c(4, 0), c(4, 0), c(0, 1), //planks
				c(5, 0), c(6, 0), c(6, 0), c(0, 1), //wood
				c(7, 0), c(7, 0), c(7, 0), c(7, 1), //leaves
				c(8, 0), c(8, 0), c(8, 0), c(0, 1), //stone
				c(9, 0), c(9, 0), c(9, 0), c(0, 1), //glass
				c(11, 0), c(11, 0), c(11, 0), c(0, 1), //diamond
				c(12, 0), c(12, 0), c(12, 0), c(0, 1), //obsidian
				c(13, 0), c(13, 0), c(13, 0), c(0, 1), //rainbow?
				c(14, 0), c(14, 0), c(14, 0), c(0, 1), //brick
				c(15, 0), c(15, 0), c(15, 0), c(0, 1), //stone brick
				c(16, 0), c(16, 0), c(16, 0), c(0, 1), //lamp 1
				c(17, 0), c(17, 0), c(17, 0), c(0, 1), //lamp 2
			};
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, atlasDescription_ssbo);
			glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(sides), &sides, GL_STATIC_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, atlasDescription_b, atlasDescription_ssbo);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
	}
	
	{//noise texture
		Image image{};
		ImageLoad("assets/noise.bmp", &image);
		glActiveTexture(GL_TEXTURE0 + noise_it);
		glBindTexture(GL_TEXTURE_2D, noise_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.get());
	}
	
	{ //main program
		glDeleteProgram(mainProgram);
		mainProgram = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromProjectFileName("shaders/main.vert", GL_VERTEX_SHADER  , "main vertex");
		sl.addShaderFromProjectFileName("shaders/main.frag", GL_FRAGMENT_SHADER, "main shader");
	
		sl.attachShaders(mainProgram);
	
		glLinkProgram(mainProgram);
		printLinkErrors(mainProgram, "main program");
		glValidateProgram(mainProgram);
	
		sl.deleteShaders();
	
		glUseProgram(mainProgram);
		
		glUniform2ui(glGetUniformLocation(mainProgram, "windowSize"), windowSize.x, windowSize.y);
		
		glUniform1f(glGetUniformLocation(mainProgram, "playerWidth" ), playerWidth );
		glUniform1f(glGetUniformLocation(mainProgram, "playerHeight"), playerHeight);
		
		time_u   = glGetUniformLocation(mainProgram, "time");

		rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
		topDir_u = glGetUniformLocation(mainProgram, "topDir");
		near_u = glGetUniformLocation(mainProgram, "near");
		far_u = glGetUniformLocation(mainProgram, "far");
		projection_u = glGetUniformLocation(mainProgram, "projection");
		toLocal_matrix_u = glGetUniformLocation(mainProgram, "toLocal");
	
		playerChunk_u = glGetUniformLocation(mainProgram, "playerChunk");
		playerInChunk_u = glGetUniformLocation(mainProgram, "playerInChunk");
		startChunkIndex_u = glGetUniformLocation(mainProgram, "startChunkIndex");
		
		playerRelativePosition_u = glGetUniformLocation(mainProgram, "playerRelativePosition");
		drawPlayer_u = glGetUniformLocation(mainProgram, "drawPlayer");
	
		Image image;
		ImageLoad("assets/atlas.bmp", &image);
	
		glActiveTexture(GL_TEXTURE0 + atlas_it);
		glBindTexture(GL_TEXTURE_2D, atlas_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.get());
		glUniform1f(glGetUniformLocation(mainProgram, "atlasTileSize"), 16); //in current block program, in block hitbox program
		glUniform1i(glGetUniformLocation(mainProgram, "atlas"), atlas_it);
		glUniform1i(glGetUniformLocation(mainProgram, "noise"), noise_it);
		
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksBlocks"), chunksBlocks_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "AtlasDescription"), atlasDescription_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksBounds"), chunksBounds_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksNeighbours"), chunksNeighbours_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksPoistions"), chunksPostions_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksAO"), chunksAO_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksSkyLighting"), chunksSkyLighting_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksBlockLighting"), chunksBlockLighting_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksNeighbourngEmitters"), chunksEmittersGPU_b);
		
		resizeGPUBuffers();
	}
	
	{ //font program
		glDeleteProgram(fontProgram);
		fontProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromCode(
		R"(#version 420
			precision mediump float;
			
			layout(location = 0) in vec2 pos_s;
			layout(location = 1) in vec2 pos_e;
			layout(location = 2) in vec2 uv_s;
			layout(location = 3) in vec2 uv_e;
			
			//out vec2 uv;
			
			out vec2 startPos;
			out vec2 endPos;
			
			out vec2 startUV;
			out vec2 endUV;
			void main(void){
				vec2 interp = vec2(gl_VertexID % 2, gl_VertexID / 2);
				gl_Position = vec4(mix(pos_s, pos_e, interp), 0, 1);
				//uv = mix(uv_s, uv_e, interp);
				startPos = pos_s;
				endPos   = pos_e;
				startUV  = uv_s ;
				endUV    = uv_e ;
			}
		)", GL_VERTEX_SHADER,"font vertex");
		
		sl.addShaderFromCode(
		R"(#version 420
			in vec4 gl_FragCoord;
			//in vec2 uv;
			
			in vec2 startPos;
			in vec2 endPos;
			
			in vec2 startUV;
			in vec2 endUV;
			
			uniform sampler2D font;
			uniform vec2 screenSize;
			
			out vec4 color;
			
			float col(vec2 coord) {
				const vec2 pos = (coord / screenSize) * 2 - 1;
				const vec2 uv = startUV + (pos - startPos) / (endPos - startPos) * (endUV - startUV);
				
				return texture2D(font, clamp(uv, startUV, endUV)).r;
			}
			
			float rand(const vec2 co) {
				return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
			}
			
			float sampleN(const vec2 coord, const uint n, const vec2 startRand) {
				const vec2 pixelCoord = floor(coord);
				const float fn = float(n);
			
				float result = 0;
				for (uint i = 0; i < n; i++) {
					for (uint j = 0; j < n; j++) {
						const vec2 curCoord = pixelCoord + vec2(i / fn, j / fn);
						const vec2 offset = vec2(rand(startRand + curCoord.xy), rand(startRand + curCoord.xy + i+1)) / fn;
						const vec2 offsetedCoord = curCoord + offset;
			
						const float sampl = col(offsetedCoord);
						result += sampl;
					}
				}
			
				return result / (fn * fn);
			}

			void main() {
				const float col = sampleN(gl_FragCoord.xy, 4, startUV);
				
				color = vec4(vec3(0), 1-col);
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
	
		glActiveTexture(GL_TEXTURE0 + font_it);
		glBindTexture(GL_TEXTURE_2D, font_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.get());
        //glBindTexture(GL_TEXTURE_2D, 0);
		
		GLuint const fontTex_u = glGetUniformLocation(fontProgram, "font");
		glUniform1i(fontTex_u, font_it);
		
		glUniform2f(glGetUniformLocation(fontProgram, "screenSize"), windowSize_d.x, windowSize_d.y);
	}
	
	{ //test program
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
	
	{ //block hitbox program
		glDeleteProgram(blockHitbox_p);
		blockHitbox_p = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromCode(
			R"(#version 420
			uniform mat4 projection;
			uniform mat4 modelMatrix;
			
			out vec2 uv;
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
			
				gl_Position = projection * (modelMatrix * vec4(xyz, 1.0));
				uv = (vec2(mirror*(1-2*(idx&1)), mirror*(1-2*(idx>>1)))+1) / 2;
			}
			)", GL_VERTEX_SHADER, "Block hitbox vertex"
		);
		
		sl.addShaderFromCode(
			R"(#version 420
			in vec2 uv;
			out vec4 color;
			
			uniform float atlasTileSize;
			uniform sampler2D atlas;
			
			vec3 sampleAtlas(const vec2 offset, const vec2 coord) { //copied from main.frag
				const ivec2 size = textureSize(atlas, 0);
				const vec2 textureCoord = vec2(coord.x + offset.x, offset.y + 1-coord.y);
				const vec2 uv_ = vec2(textureCoord * atlasTileSize / vec2(size));
				const vec2 uv = vec2(uv_.x, 1 - uv_.y);
				return pow(texture(atlas, uv).rgb, vec3(2.2));
			}

			void main() {
				const vec3 value = sampleAtlas(vec2(31), uv);
				if(dot(value, vec3(1)) / 3 > 0.9) discard;
				color = vec4(value, 0.8);
			}
			)", 
			GL_FRAGMENT_SHADER,  
			"Block hitbox fragment"
		);
	
		sl.attachShaders(blockHitbox_p);
	
		glLinkProgram(blockHitbox_p);
		glValidateProgram(blockHitbox_p);
	
		sl.deleteShaders();
	
		glUseProgram(blockHitbox_p);
		
		glUniform1i(glGetUniformLocation(blockHitbox_p, "atlas"), atlas_it);
		glUniform1f(glGetUniformLocation(blockHitbox_p, "atlasTileSize"),  16); //from main prgram
		
		blockHitboxModelMatrix_u = glGetUniformLocation(blockHitbox_p, "modelMatrix");
		blockHitboxProjection_u  = glGetUniformLocation(blockHitbox_p, "projection");
	}
	
	{ //current block program
		glDeleteProgram(currentBlockProgram);
		currentBlockProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromCode(
		R"(#version 430
			precision mediump float;
			
			uniform vec2 startPos;
			uniform vec2 endPos;
			
			void main(void){
				const vec2 verts[] = {
					vec2(0),
					vec2(1, 0),
					vec2(0, 1),
					vec2(1)
				};
				vec2 interp = verts[gl_VertexID];
				gl_Position = vec4(mix(startPos, endPos, interp), 0, 1);
			}
		)", GL_VERTEX_SHADER,"cur block vertex");
		
		sl.addShaderFromCode(
		R"(#version 430
			in vec4 gl_FragCoord;

			uniform vec2 startPos;
			uniform vec2 endPos;
			
			uniform sampler2D font;
			uniform vec2 screenSize;
			
			out vec4 color;
			
			uniform uint block;
			uniform float atlasTileSize;
			uniform sampler2D atlas;
			
			restrict readonly buffer AtlasDescription {
				int positions[]; //16bit xSide, 16bit ySide; 16bit xTop, 16bit yTop; 16bit xBot, 16bit yBot 
			};
			
			vec3 sampleAtlas(const vec2 offset, const vec2 coord) { //copied from main.frag
				const ivec2 size = textureSize(atlas, 0);
				const vec2 textureCoord = vec2(coord.x + offset.x, offset.y + 1-coord.y);
				const vec2 uv_ = vec2(textureCoord * atlasTileSize / vec2(size));
				const vec2 uv = vec2(uv_.x, 1 - uv_.y);
				return pow(texture(atlas, uv).rgb, vec3(2.2));
			}
			
			vec2 atlasAt(const uint id, const ivec3 side) {
				const int offset = int(side.y == 1) + int(side.y == -1) * 2;
				const int index = (int(id) * 4 + offset);
				const int pos = positions[index];
				const int bit16 = 65535;
				return vec2( pos&bit16, (pos>>16)&bit16 );
			}
			
			vec3 col(vec2 coord) {
				const vec2 pos = (coord / screenSize) * 2 - 1;
				const vec2 uv = (pos - startPos) / (endPos - startPos);
				
				const vec2 offset = atlasAt(block, ivec3(1,0,0));
				return sampleAtlas(offset, clamp(uv, 0.0001, 0.9999));
			}
			
			float rand(const vec2 co) {
				return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
			}
			
			vec3 sampleN(const vec2 coord, const uint n, const vec2 startRand) {
				const vec2 pixelCoord = floor(coord);
				const float fn = float(n);
			
				vec3 result = vec3(0);
				for (int i = 0; i < n; i++) {
					for (int j = 0; j < n; j++) {
						const vec2 curCoord = pixelCoord + vec2(i / fn, j / fn);
						const vec2 offset = vec2(rand(startRand + curCoord.xy), rand(startRand + curCoord.xy + i+1)) / fn;
						const vec2 offsetedCoord = curCoord + offset;
			
						const vec3 sampl = col(offsetedCoord);
						result += sampl;
					}
				}
			
				return result / (fn * fn);
			}

			void main() {
				const vec3 col = sampleN(gl_FragCoord.xy, 6, vec2(block));
				
				color = vec4(col, 1);
			}
		)",
		GL_FRAGMENT_SHADER,
		"current block fragment");
		
		sl.attachShaders(currentBlockProgram);
	
		glLinkProgram(currentBlockProgram);
		glValidateProgram(currentBlockProgram);
	
		sl.deleteShaders();
	
		glUseProgram(currentBlockProgram);
		
		glUniform1i(glGetUniformLocation(currentBlockProgram, "atlas"), atlas_it);
		cb_blockIndex_u = glGetUniformLocation(currentBlockProgram, "block");
		
		vec2d const size{ vec2d{aspect,1} * 0.06 };
		vec2d const end_ { 1 - 0.02*aspect, 1-0.02 };
		vec2d const start_{ end_ - size };
		
		vec2d const end  { (end_   * windowSize_d).floor() / windowSize_d * 2 - 1 };
		vec2d const start{ (start_ * windowSize_d).floor() / windowSize_d * 2 - 1 };

		glUniform2f(glGetUniformLocation(currentBlockProgram, "startPos"), start.x, start.y);
		glUniform2f(glGetUniformLocation(currentBlockProgram, "endPos"), end.x, end.y);
		glUniform1f(glGetUniformLocation(currentBlockProgram, "atlasTileSize"), 16); //from main program
		glUniform2f(glGetUniformLocation(currentBlockProgram, "screenSize"), windowSize_d.x, windowSize_d.y);
		
		glShaderStorageBlockBinding(currentBlockProgram, glGetProgramResourceIndex(currentBlockProgram, GL_SHADER_STORAGE_BLOCK, "AtlasDescription"), atlasDescription_b);
	}
}

template<typename T> void iterate3by3Volume(T &&action) {
	for(int i{}; i < 27; i++) {
		vec3i const dir{ (i%3)-1, ((i/3)%3)-1, ((i/9)%3)-1 };
		
		if constexpr(std::is_convertible_v<decltype(action( vec3i{}, int(0) )), bool>) {
			if(action(dir, i)) break;	
		}
		else {
			action(dir, i);
		}
	}
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
		(flatChunk.x * 1.0 * units::blocksInChunkDim + block.x) / 20.0, 
		(flatChunk.y * 1.0 * units::blocksInChunkDim + block.y) / 20.0, 
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
		 * units::blocksInChunkDim)
		.floor().clamp(0, units::blocksInChunkDim)
	);
	
	auto const height{ heightAt(flatChunk,it) };
	
	return vec3i{ it.x, int32_t(std::floor(height))+1, it.y };
}

std::string chunkFilename(chunk::Chunk const &chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save/" << pos << ".cnk";
	return ss.str();
}

std::string chunkNewFilename(chunk::Chunk const &chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save2/" << pos << ".cnk2";
	return ss.str();
}

static bool isBlockTranslucent(uint16_t const id) {
	return id == 0 || id == 5 || id == 7;
}

static bool isBlockEmitter(uint16_t const id) {
	return id == 13 || id == 14;
}

void writeChunk(chunk::Chunk &chunk) {
	std::cout << "!";
	auto const &data{ chunk.data() };
	
	std::ofstream chunkFileOut{ chunkNewFilename(chunk), std::ios::binary };
	
	for(int x{}; x < units::blocksInChunkDim; x++) 
	for(int y{}; y < units::blocksInChunkDim; y++) 
	for(int z{}; z < units::blocksInChunkDim; z++) {
		vec3i const blockCoord{x,y,z};
		auto const blockData = data[chunk::blockIndex(blockCoord)].data();
		
		uint8_t const blk[] = { 
			(unsigned char)((blockData >> 0) & 0xff), 
			(unsigned char)((blockData >> 8) & 0xff),
			(unsigned char)((blockData >> 16) & 0xff),
			(unsigned char)((blockData >> 24) & 0xff),
		};
		chunkFileOut.write(reinterpret_cast<char const *>(&blk[0]), 4);
	}
}

bool tryReadChunk(chunk::Chunk chunk, vec3i &start, vec3i &end) {
	auto &data{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	
	auto const filename2{ chunkNewFilename(chunk) };
	std::ifstream chunkFileIn2{ filename2, std::ios::binary };	
	if(!chunkFileIn2.fail()) {
		for(int x{}; x < units::blocksInChunkDim; x++) 
		for(int y{}; y < units::blocksInChunkDim; y++) 
		for(int z{}; z < units::blocksInChunkDim; z++) 
		{
			vec3i const blockCoord{x,y,z};
			//uint16_t &block = data[chunk::blockIndex(blockCoord)];
			uint8_t blk[4];
			
			chunkFileIn2.read( reinterpret_cast<char *>(&blk[0]), 4 );
			
			uint32_t const block( 
				  (uint32_t(blk[0]) << 0 )
				| (uint32_t(blk[1]) << 8 )
				| (uint32_t(blk[2]) << 16)
				| (uint32_t(blk[3]) << 24)					
			);
			
			data[chunk::blockIndex(blockCoord)] = chunk::Block(block);
			if(block != 0) {
				start = start.min(blockCoord);
				end   = end  .max(blockCoord);
			}
			
			if(isBlockEmitter(block)) emitters.add(blockCoord);
		}

		return true;
	}
	chunkFileIn2.close();
	
	auto const filename{ chunkFilename(chunk) };
	std::ifstream chunkFileIn{ filename, std::ios::binary };
	if(!chunkFileIn.fail()) {
		for(int x{}; x < units::blocksInChunkDim; x++) 
		for(int y{}; y < units::blocksInChunkDim; y++) 
		for(int z{}; z < units::blocksInChunkDim; z++) {
			vec3i const blockCoord{x,y,z};
			//uint16_t &block = data[chunk::blockIndex(blockCoord)];
			uint8_t blk[2];
			
			chunkFileIn.read( reinterpret_cast<char *>(&blk[0]), 2 );
			
			uint16_t const block( blk[0] | (uint16_t(blk[1]) << 8) );
			
			data[chunk::blockIndex(blockCoord)] = chunk::Block::fullBlock(block);
			if(block != 0) {
				start = start.min(blockCoord);
				end   = end  .max(blockCoord);
			}
			
			if(isBlockEmitter(block)) emitters.add(blockCoord);
		}

		return true;
	}
	
	
	return false;
}

void genTrees(chunk::Chunk chunk, vec3i &start, vec3i &end) {	
	auto const &chunkCoord{ chunk.position() };
	auto &data{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	
	for(int32_t cx{-1}; cx <= 1; cx ++) 
	for(int32_t cz{-1}; cz <= 1; cz ++) {
		vec3i const chunkOffset{ cx, -chunkCoord.y, cz };
		auto const curChunk{ chunkCoord + chunkOffset };
		
		auto const treeBlock{ getTreeBlock(vec2i{curChunk.x, curChunk.z}) };
		
		for(int32_t x{-2}; x <= 2; x++) 
		for(int32_t y{0} ; y < 6 ; y++) 
		for(int32_t z{-2}; z <= 2; z++) {
			vec3i tl{ x,y,z };// tree-local block
			auto const blk{ chunkOffset * units::blocksInChunkDim + treeBlock + tl };
			
			if(blk.inMMX(vec3i{0}, vec3i{units::blocksInChunkDim}).all()) {
				auto const index{ chunk::blockIndex(blk) };
				chunk::Block &curBlock{ data[index] };
				
				if(curBlock.id() == 0) {
					bool is = false;
					if((is = tl.x == 0 && tl.z == 0 && tl.y <= 4)) curBlock = chunk::Block::fullBlock(4);
					else if((is = 
							(tl.y >= 2 && tl.y <= 3
							&& !( (abs(x) == abs(z))&&(abs(x)==2) )
							) || 
							(tl.in(vec3i{-1, 4, -1}, vec3i{1, 5, 1}).all()
							&& !( (abs(x) == abs(z))&&(abs(x)==1) &&(tl.y==5 || (treeBlock.x*(x+1)/2+treeBlock.z*(z+1)/2)%2==0) )
							)
					)) curBlock = chunk::Block::fullBlock(5);
					
					if(isBlockEmitter(curBlock.id())) emitters.add(blk);

					if(is) {
						start = start.min(blk);
						end   = end  .max(blk);
					}
				}
			}
		}
	}
	
}

void genChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk, vec3i &start, vec3i &end) {
	auto const &pos{ chunk.position() };
	auto &blocks{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	
	for(int z{}; z < units::blocksInChunkDim; ++z)
	for(int y{}; y < units::blocksInChunkDim; ++y) 
	for(int x{}; x < units::blocksInChunkDim; ++x) {
		vec3i const blockCoord{ x, y, z };
		
		auto const height{ heights[z * units::blocksInChunkDim + x] };
		
		//if(misc::mod(int32_t(height), 9) == misc::mod((pos.y * units::blocksInChunkDim + y + 1), 9)) { //repeated floor
		double const diff{ height - double(pos.y * units::blocksInChunkDim + y) };
		if(diff >= 0) {
			uint16_t block;
			
			if(diff < 1) block = 1; //grass
			else if(diff < 5) block = 2; //dirt
			else block = 6; //stone
			
			start = start.min(blockCoord);
			end   = end  .max(blockCoord);
			
			blocks[blockCoord] = chunk::Block::fullBlock(block);
		}
		else {
			blocks[blockCoord] = chunk::Block::emptyBlock();
		}
		
		
		if(isBlockEmitter(blocks[blockCoord].id())) emitters.add(blockCoord);
	}
		
	genTrees(chunk, *&start, *&end);	
}

static void fillChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk) {
	auto &aabb{ chunk.aabb() };
	
	vec3i start{units::blocksInChunkDim-1};
	vec3i end  {0 };
	
	if(loadChunks && tryReadChunk(chunk, start, end)) chunk.modified() = false;
	else { genChunkData(heights, chunk, start, end); chunk.modified() = true; }
	
	aabb = chunk::AABB(start, end);
}

struct SkyLightingConfig {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk) {
		return chunk.skyLighting();
	}
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) { 
		return getLighting(chunk)[cubeInChunkCoord]; 
	}
	
	static LightingCubeType getType(uint16_t const blockId, bool const cube) { 
		if(!cube || isBlockTranslucent(blockId)) return LightingCubeType::medium;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		return (fromDir == vec3i{0,-1,0}) ? lighting : uint8_t(misc::max(int(lighting) - 1, 0));
	}
};

struct BlocksLightingConfig {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk) {
		return chunk.blockLighting();
	}
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) { 
		return getLighting(chunk)[cubeInChunkCoord]; 
	}
	
	static LightingCubeType getType(uint16_t const blockId, bool const cube) { 
		if(!cube || isBlockTranslucent(blockId)) return LightingCubeType::medium;
		else if(isBlockEmitter(blockId)) return LightingCubeType::emitter;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		return uint8_t(misc::max(int(lighting) - 1, 0));
	}
};

template<typename Config>
static void setNeighboursLightingUpdate(chunk::Chunks &chunks, vec3i const minChunkPos, vec3i const maxChunkPos) {
	static vec3i const sides[3]       = { {1,0,0}, {0,1,0}, {0,0,1} };
	static vec3i const otherSides1[3] = { {0,0,1}, {0,0,1}, {0,1,0} };
	static vec3i const otherSides2[3] = { {0,1,0}, {1,0,0}, {1,0,0} };
	
	for(int axisPositive{}; axisPositive < 2; axisPositive++) 
	for(int side{}; side < 3; side++) {
	   for(auto chunkCoord1{minChunkPos.dot(otherSides1[side])}; chunkCoord1 <= maxChunkPos.dot(otherSides1[side]); chunkCoord1++)
		for(auto chunkCoord2{minChunkPos.dot(otherSides2[side])}; chunkCoord2 <= maxChunkPos.dot(otherSides2[side]); chunkCoord2++) {
			auto const neighbourDir{ sides[side] * (axisPositive*2 - 1) };
			
			auto const chunkCoord{
				  (axisPositive ? maxChunkPos : minChunkPos) * sides[side]
				+ otherSides1[side] * chunkCoord1
				+ otherSides2[side] * chunkCoord2
			};
			auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunks, chunkCoord}.optChunk().get() };
			if(chunkIndex == -1) continue;
			auto const chunk{ chunks[chunkIndex] };
			
			auto const neighbourChunkCoord{ chunkCoord + neighbourDir };
			auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.move(neighbourChunkCoord).get() };	
			if(neighbourChunkIndex == -1) continue;
			auto neighbourChunk{ chunks[neighbourChunkIndex] };
			
			auto const &neighbourBlocks{ neighbourChunk.data() };
			
			for(int coord1{}; coord1 < units::cubesInChunkDim; coord1++)
			for(int coord2{}; coord2 < units::cubesInChunkDim; coord2++) {
				auto const cubeInChunkCoord{
					  sides[side] * (axisPositive ? units::cubesInChunkDim-1 : 0)
					+ otherSides1[side] * coord1
					+ otherSides2[side] * coord2
				};
				
				auto const neighbourCubeInChunkCoord{
					  sides[side] * (axisPositive ? 0 : units::cubesInChunkDim-1)
					+ otherSides1[side] * coord1
					+ otherSides2[side] * coord2
				};
				
				auto const neighbourCube{ neighbourBlocks.cubeAt(neighbourCubeInChunkCoord) };
				auto const neighbourBlockId{ neighbourCube.block.id() };
				auto const neighbourBlockCube{ neighbourCube.isSolid };
				
				auto const type{ Config::getType(neighbourBlockId, neighbourBlockCube) };
				
				if(type != LightingCubeType::medium) continue;
				
				auto const cubeLight{ Config::getLight(chunk, cubeInChunkCoord) };
				auto const toNeighbourCubeLightingLevel{ Config::propagationRule(cubeLight, neighbourDir, neighbourBlockId, neighbourBlockCube) };
				if(toNeighbourCubeLightingLevel == 0) continue;
				
				auto &neighbourCubeLight{ Config::getLight(neighbourChunk, neighbourCubeInChunkCoord) };
				if(toNeighbourCubeLightingLevel <= neighbourCubeLight) continue;
				
				neighbourChunk.status().setUpdateLightingAdd(true);
				goto nextChunk;
			}
		   nextChunk: continue;
		}
	}
}

template<typename Config> //TODO change fromCube and other to vec3i
static void fastUpdateLightingInDir(
	chunk::Chunk chunk, 
	vec3i const dir, vec3l const otherAxis1, vec3l const otherAxis2, 
	vec3l const fromCube, vec3l const toCube, 
	vec3l &minCube, vec3l &maxCube
) {
	auto const chunkIndex{ chunk.chunkIndex() };
	auto const chunkCoord{ chunk.position() };
	auto &chunks{ chunk.chunks() };
	auto &lighting{ Config::getLighting(chunk) };
	
	vec3l const axis{ dir.abs() };
	auto const dirPositive{ (dir > 0).any() };
	
	
	vec3l const beginCoordsCubeAxisUnbound{
		fromCube.dot(axis),
		fromCube.dot(otherAxis1),
		fromCube.dot(otherAxis2)
	}; /*coords along specified axis, in cubes*/
	vec3l const endCoordsCubeAxisUnbound{
		toCube.dot(axis),
		toCube.dot(otherAxis1),
		toCube.dot(otherAxis2)
	};
	
	vec3l const chunkCoordAxis( vec3l(chunkCoord).dot(axis), vec3l(chunkCoord).dot(otherAxis1), vec3l(chunkCoord).dot(otherAxis2) );/*
		chunk coords alond specified axis, in chunks
	*/
	
	auto const chunkFirstCoordCubeAxis{  chunkCoordAxis    * units::cubesInChunkDim };
	auto const chunkLastCoordCubeAxis { (chunkCoordAxis+1) * units::cubesInChunkDim - 1 };
	
	auto const beginCoordsAxis{ beginCoordsCubeAxisUnbound.clamp(chunkFirstCoordCubeAxis, chunkLastCoordCubeAxis) };
	auto const endCoordsAxis  { endCoordsCubeAxisUnbound  .clamp(chunkFirstCoordCubeAxis, chunkLastCoordCubeAxis) };
	
	auto const dirCoordDiff{ endCoordsAxis.x - beginCoordsAxis.x };
	
	
	chunk::Move_to_neighbour_Chunk const mtnChunk{chunk};
	
	auto const startCubeCoord{
		  axis       * (dirPositive ? beginCoordsAxis.x : endCoordsAxis.x)
		+ otherAxis1 * beginCoordsAxis.y
		+ otherAxis2 * beginCoordsAxis.z
	};
	auto const beforeStartCubeCoord{ vec3i(startCubeCoord - vec3l(dir)) };
	pos::Cube const beforeStartCubePos{ beforeStartCubeCoord };
	auto const chunkBeforeIndex{ chunk::Move_to_neighbour_Chunk{mtnChunk}.move(beforeStartCubePos.valAs<pos::Chunk>()).get() };
	
	auto const isBefore{ chunkBeforeIndex != chunkIndex };
	
	static /*constexpr*/ chunk::ChunkLighting const skyLighting{ 31u };
	auto const canUseSkyLighting{ std::is_same_v<SkyLightingConfig, Config> && dir == vec3i{0,-1,0} };/*
		this check is not preformed in other places where it should be preformed (for example in AddLighting::)
	*/
	
	if((canUseSkyLighting || chunkBeforeIndex != -1) && isBefore) {
		auto &lightingBefore{ 
			chunkBeforeIndex == -1 /*=> canUseSkyLighting*/ ? 
			  skyLighting 
			: Config::getLighting(chunks[chunkBeforeIndex])
		};
		{
			for(auto o1{beginCoordsAxis.y}; o1 <= endCoordsAxis.y; o1++)
			for(auto o2{beginCoordsAxis.z}; o2 <= endCoordsAxis.z; o2++) {
				auto const cubeCoord{
					axis       * (dirPositive ? beginCoordsAxis.x : endCoordsAxis.x)
					+ otherAxis1 * o1
					+ otherAxis2 * o2
				};
				vec3i const cubeInChunkCoord(
					cubeCoord - vec3l(chunkCoord) * units::cubesInChunkDim
				);
				
				auto const cube{ chunk.data().cubeAt(cubeInChunkCoord) };
				auto const type{ Config::getType(cube.block.id(), cube.isSolid) };
				if(type != LightingCubeType::medium) continue;

				auto const cubeCoordBefore{ (cubeInChunkCoord - dir).mod(units::cubesInChunkDim) };
				
				auto const candLighting{ Config::propagationRule(lightingBefore[cubeCoordBefore], dir, cube.block.id(), cube.isSolid) };
				auto &cubeLighting{ lighting[cubeInChunkCoord] };
				
				if(cubeLighting < candLighting) {
					cubeLighting = candLighting;
					
					minCube = minCube.min(cubeCoord);
					maxCube = maxCube.max(cubeCoord);
				}
			}
		}
	}
	
	for(auto i{decltype(dirCoordDiff)(isBefore)}; i <= dirCoordDiff; i++) {
		for(auto o1{beginCoordsAxis.y}; o1 <= endCoordsAxis.y; o1++)
		for(auto o2{beginCoordsAxis.z}; o2 <= endCoordsAxis.z; o2++) {
			auto const cubeCoord{
				  axis       * (dirPositive ? (beginCoordsAxis.x + i) : (endCoordsAxis.x - i))
				+ otherAxis1 * o1
				+ otherAxis2 * o2
			};
			vec3i const cubeInChunkCoord(
				cubeCoord - vec3l(chunkCoord) * units::cubesInChunkDim
			);
			
			auto const cube{ chunk.data().cubeAt(cubeInChunkCoord) };
			auto const blockId{ cube.block.id() };
			auto const isCube{ cube.isSolid };
			
			auto const type{ Config::getType(blockId, isCube) };
			if(type != LightingCubeType::medium) continue;
			
			auto const cubeCoordBefore{ (cubeInChunkCoord - dir) };
			
			auto const candLighting{ Config::propagationRule(lighting[cubeCoordBefore], dir, blockId, isCube) };
			auto &cubeLighting{ lighting[cubeInChunkCoord] };
			
			auto const updateLight{ cubeLighting < candLighting };
			if(updateLight) {
				cubeLighting = candLighting;
				minCube = minCube.min(cubeCoord);
				maxCube = maxCube.max(cubeCoord);
			}		
		}
	}
}

static void updateSkyLightingInChunks(chunk::Chunks &chunks, vec3i const minChunkPos, vec3i const maxChunkPos) {	
	static std::vector<int> chunkIndices{};
	chunkIndices.clear();
	
	for(int x{ minChunkPos.x }; x <= maxChunkPos.x; x++)
	for(int z{ minChunkPos.z }; z <= maxChunkPos.z; z++)
	for(int y{ maxChunkPos.y }; y >= minChunkPos.y; y--
		/*add chunks from top to bottom, for easier propagation of light downwards*/) {
		vec3i const curChunkPos{ x, y, z };
		auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunks}.move(curChunkPos).get() };
		
		if(neighbourChunkIndex != -1) {
			chunkIndices.push_back(neighbourChunkIndex);
			
			auto curChunk{ chunks[neighbourChunkIndex] };
			curChunk.status().setLightingUpdated(true);
		}
	}
	
	int const chunkIndicesCount(chunkIndices.size());
	if(chunkIndicesCount == 0) return;
	
	vec3l minCube{ vec3l(minChunkPos  ) * units::cubesInChunkDim-1 };
	vec3l maxCube{ vec3l(maxChunkPos+1) * units::cubesInChunkDim };
	
	for(int iters{}; iters < 10; iters++) {
		vec3l newMinCube{ vec3l(maxChunkPos + 1) * units::cubesInChunkDim };
		vec3l newMaxCube{ vec3l(minChunkPos    ) * units::cubesInChunkDim-1 };
		
		for(int dir{1}; dir >= -1; dir-=2) {
			for(int j{}; j < 3; j++) {
				for(int i = 0; i < chunkIndicesCount; i ++) {
					static vec3i const updateDirs[] = {
						{0,-1,0},
						{+1,0,0},
						{0,0,+1},
					};
					static vec3l const otherDirs1[] = { {0,0,1}, {0,0,1}, {0,1,0} };
					static vec3l const otherDirs2[] = { {1,0,0}, {0,1,0}, {1,0,0} };
					
					auto const curChunk{ chunks[chunkIndices[i]] };
					fastUpdateLightingInDir<SkyLightingConfig>(
						curChunk, 
						updateDirs[j]*dir, otherDirs1[j], otherDirs2[j],
						minCube, maxCube, 
						*&newMinCube, *&newMaxCube
					);
				}
			}
		}
		
		if((newMinCube > newMaxCube).any()/*empty*/) return;
		else {
			minCube = newMinCube;
			maxCube = newMaxCube;
		}
	}
	
	auto lastChunkIndex{ chunkIndices[0] };
	
	//x, y, z should be ints, but fastUpdateLightingInDir expects minCube and maxCube as vec3l. TODO: fix fastUpdateLightingInDir
	for(int z(minCube.z); z <= maxCube.z; z++)
	for(int y(minCube.y); y <= maxCube.y; y++)
	for(int x(minCube.x); x <= maxCube.x; x++) {
		pos::Cube const cubePos{{x, y, z}};
		auto const cubeChunkCoord{ cubePos.valAs<pos::Chunk>() };
		auto const cubeInChunkCoord{ cubePos.in<pos::Chunk>().value() };
		
		lastChunkIndex = chunk::Move_to_neighbour_Chunk{chunks, chunk::OptionalChunkIndex{lastChunkIndex}}.move(cubeChunkCoord).get();	
		if(lastChunkIndex == -1) continue;
		
		auto chunk{ chunks[lastChunkIndex] };
		
		AddLighting::fromCube<SkyLightingConfig>(chunk, cubeInChunkCoord);
	}
}

static void updateBlockLightingInChunks(chunk::Chunks &chunks, vec3i const minChunkPos, vec3i const maxChunkPos) {
	//TODO: this function is not much different from updateSkyLightingInChunks. It would be better to join them into one/
		
	static std::vector<int> chunkIndices{};
	chunkIndices.clear();
	
	for(int x{ minChunkPos.x }; x <= maxChunkPos.x; x++)
	for(int z{ minChunkPos.z }; z <= maxChunkPos.z; z++)
	for(int y{ minChunkPos.y }; y <= maxChunkPos.y; y++) {
		vec3i const curChunkPos{ x, y, z };
		auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunks}.move(curChunkPos).get() };
		
		if(neighbourChunkIndex != -1) {
			chunkIndices.push_back(neighbourChunkIndex);
			
			auto curChunk{ chunks[neighbourChunkIndex] };
			curChunk.status().setLightingUpdated(true);
			
			auto &blockLighting{ curChunk.blockLighting() };
			for(chunk::ChunkBlocksList::value_type const blockIndex : curChunk.emitters()) {
				auto const blockInChunkCoord{ chunk::indexBlock(blockIndex) };
				for(int cubeInBlockIndex{}; cubeInBlockIndex < pos::cubesInBlockCount; cubeInBlockIndex++) {
					auto const cubeInBlockCoord{ chunk::Block::cubeIndexPos(cubeInBlockIndex) };
					
					auto const cubeInChunkCoord{ blockInChunkCoord*units::cubesInBlockDim + cubeInBlockCoord };
					
					blockLighting[cubeInChunkCoord] = 31u;
				}
			}
		}
	}
	
	int const chunkIndicesCount(chunkIndices.size());
	if(chunkIndicesCount == 0) return;
	
	vec3l minCube{ vec3l(minChunkPos  ) * units::cubesInChunkDim-1 };
	vec3l maxCube{ vec3l(maxChunkPos+1) * units::cubesInChunkDim };
	
	for(int iters{}; iters < 10; iters++) {
		vec3l newMinCube{ vec3l(maxChunkPos + 1) * units::cubesInChunkDim };
		vec3l newMaxCube{ vec3l(minChunkPos    ) * units::cubesInChunkDim-1 };
		
		for(int dir{1}; dir >= -1; dir-=2) {
			for(int j{}; j < 3; j++) {
				for(int i = 0; i < chunkIndicesCount; i ++) {
					static vec3i const updateDirs[] = {
						{0,-1,0},
						{+1,0,0},
						{0,0,+1},
					};
					static vec3l const otherDirs1[] = { {0,0,1}, {0,0,1}, {0,1,0} };
					static vec3l const otherDirs2[] = { {1,0,0}, {0,1,0}, {1,0,0} };
					
					auto const curChunk{ chunks[chunkIndices[i]] };
					fastUpdateLightingInDir<BlocksLightingConfig>(
						curChunk, 
						updateDirs[j]*dir, otherDirs1[j], otherDirs2[j],
						minCube, maxCube, 
						*&newMinCube, *&newMaxCube
					);
				}
			}
		}
		
		if((newMinCube > newMaxCube).any()/*empty*/) return;
		else {
			minCube = newMinCube;
			maxCube = newMaxCube;
		}
	}
	
	auto lastChunkIndex{ chunkIndices[0] };
	
	//x, y, z should be ints, but fastUpdateLightingInDir expects minCube and maxCube as vec3l. TODO: fix fastUpdateLightingInDir
	for(int z(minCube.z); z <= maxCube.z; z++)
	for(int y(minCube.y); y <= maxCube.y; y++)
	for(int x(minCube.x); x <= maxCube.x; x++) {
		pos::Cube const cubePos{{x, y, z}};
		auto const cubeChunkCoord{ cubePos.valAs<pos::Chunk>() };
		auto const cubeInChunkCoord{ cubePos.in<pos::Chunk>().value() };
		
		lastChunkIndex = chunk::Move_to_neighbour_Chunk{chunks, chunk::OptionalChunkIndex{lastChunkIndex}}.move(cubeChunkCoord).get();	
		if(lastChunkIndex == -1) continue;
		
		auto chunk{ chunks[lastChunkIndex] };
		
		AddLighting::fromCube<BlocksLightingConfig>(chunk, cubeInChunkCoord);
	}
}

static void updateNeighbouringEmitters(chunk::Chunk chunk) {
	auto &chunks{ chunk.chunks() };
	struct NeighbourChunk {
		vec3i dir;
		int chunkIndex;
		int emittersCount;
	};
				
	NeighbourChunk neighbours[3*3*3];
	int neighboursCount{};
	int totalEmittersCount{};
	
	
	iterate3by3Volume([&](vec3i const neighbourDir, int const i) {
		auto const neighbourIndex{ chunk::Move_to_neighbour_Chunk{ chunk }.moveToNeighbour(neighbourDir) };
		if(neighbourIndex) {
			auto const neighbourChunk{ chunks[neighbourIndex.get()] };
			auto const emittersCount{ neighbourChunk.emitters().size() };
			
			totalEmittersCount += emittersCount;
			neighbours[neighboursCount++] = { neighbourDir, neighbourChunk.chunkIndex(), emittersCount };
		}
	});
	
	auto &curChunkNeighbouringEmitters{ chunk.neighbouringEmitters() };
	if(totalEmittersCount == 0) { curChunkNeighbouringEmitters.clear(); return; }
	
	std::array<vec3i, chunk::Chunk3x3BlocksList::capacity> neighbouringEmitters{};
	int const neighbouringEmittersCount{ std::min<int>(neighbouringEmitters.size(), totalEmittersCount) };
	int const emitterStep{ std::max<int>(totalEmittersCount / neighbouringEmitters.size(), 1) };
	
	int neighbouringEmitterToAdd( emitterStep > 1 ? totalEmittersCount % neighbouringEmitters.size() : 0 );
	int startChunkEmiter{};
	int currentEmitterNeighbourIndex{};
	for(int i{}; i < neighbouringEmittersCount;) {
		assert(currentEmitterNeighbourIndex < neighboursCount);
		auto const neighbour{ neighbours[currentEmitterNeighbourIndex] };
		
		if(neighbouringEmitterToAdd < startChunkEmiter + neighbour.emittersCount) {
			auto const localChunkEmitterIndex{ neighbouringEmitterToAdd - startChunkEmiter };
			auto const localChunkEmitterBlock{ chunks[neighbour.chunkIndex].emitters()(localChunkEmitterIndex) };
			neighbouringEmitters[i] = localChunkEmitterBlock + neighbour.dir * units::blocksInChunkDim;
			neighbouringEmitterToAdd += emitterStep;
			i++;
		}
		else {
			startChunkEmiter += neighbour.emittersCount;
			currentEmitterNeighbourIndex++;
		}
	}
	
	curChunkNeighbouringEmitters.fillRepeated(neighbouringEmitters, neighbouringEmittersCount);
}

static uint8_t calcAO(chunk::Chunks &chunks, chunk::Move_to_neighbour_Chunk chunk, vec3i const chunkPos, vec3i const cubeCoordInChunk) {
	uint8_t cubes{};
	for(int j{}; j < 8; j ++) {
		auto const offsetcubeCoordInChunk_unnormalized{ cubeCoordInChunk + chunk::ChunkAO::dirsForIndex(j).min(0) };
		auto const offsetCubePos{ pos::Chunk{chunkPos} + pos::Cube{ offsetcubeCoordInChunk_unnormalized } };
		
		auto const offsetCubeChunkIndex{ chunk.move(offsetCubePos.valAs<pos::Chunk>()) };
		if(!offsetCubeChunkIndex.is()) continue;
		
		auto const offsetCubeBlockCoord{ offsetCubePos.as<pos::Block>().valIn<pos::Chunk>() };
		auto const offsetCubeLocalCoord{ offsetCubePos.valAs<pos::Cube>() & 1 };
		
		auto offsetCubeChunk{ chunks[offsetCubeChunkIndex.get()] };
		
		auto const solidCube{ offsetCubeChunk.data()[chunk::blockIndex(offsetCubeBlockCoord)].cube(vec3i(offsetCubeLocalCoord)) };
		
		if(solidCube) cubes |= 1 << j;
	}
	
	return cubes;
}

static void updateBlocks(chunk::Chunk chunk) {	
	auto &chunks{ chunk.chunks() };
	auto const aabb{ chunk.aabb() };
	auto const start{ aabb.start() };
	auto const end{ aabb.end() };
	auto const chunkCoord{ chunk.position() };
	auto &blocks{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	emitters.clear();  //hope that chunk is not filled with emitters
	auto &ao{ chunk.ao() };
	ao.reset();
	
	chunk::Move_to_neighbour_Chunk const mtnChunk{chunk};
	if(!aabb.empty()) {
		//AO
		static constexpr int cnb{ units::cubesInBlockDim };
		for(int z{start.z*cnb}; z < (end.z+1)*cnb; z++) 
		for(int y{start.y*cnb}; y < (end.y+1)*cnb; y++) 
		for(int x{start.x*cnb}; x < (end.x+1)*cnb; x++) {
			vec3i const cubeCoordInChunk{ x, y, z };
			ao[cubeCoordInChunk] = calcAO(chunks, mtnChunk, chunkCoord, cubeCoordInChunk);
		}
		
		
		auto const offsetedStart{ start.max(1) };
		auto const offsetedEnd  { start.min(units::blocksInChunkDim-1) };
		//blocks with no neighbours
		for(int z{offsetedStart.z}; z < (offsetedEnd.z+1); z++) 
		for(int y{offsetedStart.y}; y < (offsetedEnd.y+1); y++) 
		for(int x{offsetedStart.x}; x < (offsetedEnd.x+1); x++) {
			vec3i const blockInChunkCoord{ x, y, z };
			auto &chunkBlocks{ chunk.data() };
			auto &block{ chunkBlocks[blockInChunkCoord] };
			
			bool noNeighbours{};
			
			if(block.isEmpty()) {
				noNeighbours = true;
				
				iterate3by3Volume([&](vec3i const neighbourDir, int const index) -> bool {
					auto const neighbourBlockInChunk{ blockInChunkCoord + neighbourDir };
			
					if(!chunkBlocks[neighbourBlockInChunk].isEmpty()) {
						noNeighbours = false;
						return true;//break
					}
					
					return false;
				});
			}
			
			if(noNeighbours) block = chunk::Block::noNeighboursBlock(block);
			else             block = chunk::Block::  neighboursBlock(block);
		}
		
		for(int z{start.z}; z <= end.z; z++) 
		for(int y{start.y}; y <= end.y; y++) 
		for(int x{start.x}; x <= end.x; x++) {
			vec3i const blockInChunkCoord{ x, y, z };
			if(isBlockEmitter(blocks[blockInChunkCoord].id())) emitters.add(blockInChunkCoord);
		}
		
		iterate3by3Volume([&](vec3i const dir, int const i) {
			auto const neighbourIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(dir).get() };
			if(neighbourIndex != -1) {
				chunks[neighbourIndex].status().setUpdateNeighbouringEmitters(true);
			}
		});
	}
}


static bool updateChunk(chunk::Chunk chunk, vec3i const cameraChunkCoord, bool const maxUpdated, bool &showUpdated) {
	auto const chunkIndex{ chunk.chunkIndex() };
	chunk::ChunkData &chunkData{ chunk.data() };
	vec3i const &chunkCoord{ chunk.position() };
	auto &status{ chunk.status() };
	
	bool modified = false;
	
	if(!maxUpdated && status.needsUpdate()) { 
		auto &chunks{ chunk.chunks() };
		
		if(status.isUpdateBlocks()) {
			updateBlocks(chunk); 
			status.setUpdateBlocks(false);
			status.setBlocksUpdated(true);
		}
		if(status.isUpdateLightingAdd()) {
			updateSkyLightingInChunks(chunks, chunkCoord, chunkCoord);
			setNeighboursLightingUpdate<SkyLightingConfig>(chunks, chunkCoord, chunkCoord);
			
			updateSkyLightingInChunks(chunks, chunkCoord, chunkCoord);
			setNeighboursLightingUpdate<BlocksLightingConfig>(chunks, chunkCoord, chunkCoord);
			
			status.setUpdateLightingAdd(false);
			status.setLightingUpdated(true);
		}
		if(status.isUpdateNeighbouringEmitters()) {
			updateNeighbouringEmitters(chunk);
			
			status.setUpdateNeighbouringEmitters(false);
			status.setNeighbouringEmittersUpdated(true);
		}
		
		modified = true; 
	}
	
	if(status.isInvalidated() || !status.isFullyLoadedGPU()) {
		auto const inRenderDistance{ (chunkCoord - cameraChunkCoord).in(vec3i{-viewDistance}, vec3i{viewDistance}).all() };
						
		if(cameraChunkCoord != chunkCoord && (maxUpdated || !inRenderDistance)) {
			if(status.isStubLoadedGPU()) return modified;
			if(status.isFullyLoadedGPU() && status.isInvalidated()) return modified;

			chunk::Neighbours const neighbours{};
			static_assert(sizeof(neighbours) == sizeof(int32_t) * chunk::Neighbours::neighboursCount);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo); 
			glBufferSubData(
				GL_SHADER_STORAGE_BUFFER, 
				sizeof(int32_t) * chunk::Neighbours::neighboursCount * chunkIndex, 
				chunk::Neighbours::neighboursCount * sizeof(uint32_t), 
				&neighbours
			);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			status.markStubLoadedGPU();
			return modified;
		}
		
		
		if(status.isBlocksUpdated()) {
			uint32_t const aabbData{ chunk.aabb().getData() };
			auto const &neighbours{ chunk.neighbours() };
			auto const &ao{ chunk.ao() };

			static_assert(sizeof chunkData == 16384);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlocks_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(chunkData) * chunkIndex, sizeof(chunkData), &chunkData);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * chunkIndex, sizeof(uint32_t), &aabbData);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksPostions_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(vec3i) * chunkIndex, sizeof(vec3i), &chunkCoord);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
			static_assert(sizeof(neighbours) == sizeof(int32_t) * chunk::Neighbours::neighboursCount);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo); 
			glBufferSubData(
				GL_SHADER_STORAGE_BUFFER, 
				sizeof(int32_t) * chunk::Neighbours::neighboursCount * chunkIndex, 
				chunk::Neighbours::neighboursCount * sizeof(uint32_t), 
				&neighbours
			);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			static_assert(sizeof(chunk::ChunkAO) == sizeof(uint8_t) * chunk::ChunkAO::size);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksAO_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, chunkIndex * sizeof(chunk::ChunkAO), sizeof(chunk::ChunkAO), &ao);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	

			status.setBlocksUpdated(false);
		}					

		if(status.isLightingUpdated()) {
			showUpdated = true;
			auto const &skyLighting{ chunk.skyLighting() };
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksSkyLighting_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, chunkIndex * sizeof(chunk::ChunkLighting), sizeof(chunk::ChunkLighting), &skyLighting);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
			
			auto const &blockLighting{ chunk.blockLighting() };
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlockLighting_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, chunkIndex * sizeof(chunk::ChunkLighting), sizeof(chunk::ChunkLighting), &blockLighting);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			status.setLightingUpdated(false);
		}
		
		if(status.isNeighbouringEmittersUpdated()) {		
			auto const &emittersGPU{ chunk.neighbouringEmitters() };
			
			static_assert(sizeof(chunk::Chunk3x3BlocksList) == sizeof(uint32_t)*16);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, chunkIndex * sizeof(chunk::Chunk3x3BlocksList), sizeof(chunk::Chunk3x3BlocksList), &emittersGPU);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			status.setNeighbouringEmittersUpdated(false);
		}
		
		
		modified = true;
		status.markFullyLoadedGPU();
	}
	
	return modified;
}

static void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition) {
	double heights[units::blocksInChunkDim * units::blocksInChunkDim];
	auto minHeight{ std::numeric_limits<double>::infinity() };
	for(int z = 0; z < units::blocksInChunkDim; z++) 
	for(int x = 0; x < units::blocksInChunkDim; x++) {
		auto const height{  heightAt(vec2i{columnPosition.x,columnPosition.y}, vec2i{x,z}) };
		heights[z* units::blocksInChunkDim + x] = height;
		minHeight = std::min(minHeight, height);
	}
	
	vec3i const neighbourDirs[] = { 
		vec3i{-1,0,0}, vec3i{+1,0,0}, vec3i{0,0,-1}, vec3i{0,0,+1}
	};
	
	chunk::Move_to_neighbour_Chunk neighbourChunks[] = {              
		{chunks, vec3i{columnPosition.x, 15, columnPosition.y} + neighbourDirs[0]},
		{chunks, vec3i{columnPosition.x, 15, columnPosition.y} + neighbourDirs[1]},
		{chunks, vec3i{columnPosition.x, 15, columnPosition.y} + neighbourDirs[2]},
		{chunks, vec3i{columnPosition.x, 15, columnPosition.y} + neighbourDirs[3]}
	};
	chunk::OptionalChunkIndex topNeighbourIndex{};
	
	int chunkIndices[15 + 16];
	
	int32_t lowestNotFullY{ 15 + 1 };
	int32_t highestNotEmptyY{ -16 - 1 };
	bool emptyBefore{ true };

	int32_t highestWithBlockLighting{ -16 -1 };
	int32_t lowestWithBlockLighting{ 15 + 1 };
	for(int32_t y = 15; y >= -16; y--) {
		int32_t const usedIndex{ chunks.reserve() };
		
		auto const chunkIndex{ chunks.usedChunks()[usedIndex] };
		vec3i chunkPosition{ vec3i{columnPosition.x, y, columnPosition.y} };
		
		chunks.chunksIndex_position[chunkPosition] = chunkIndex;
		chunkIndices[y + 16] = chunkIndex;
		
		auto chunk{ chunks[chunkIndex] };
		
		chunk.position() = chunkPosition;
		
		chunk.status() = chunk::ChunkStatus{};
		chunk.status().setUpdateBlocks(true);
		chunk.status().setLightingUpdated(true);
		chunk.blockLighting().reset();
		chunk.emitters().clear();
		chunk.neighbouringEmitters().clear();
		
		auto &neighbours_{ chunk.neighbours() };
		chunk::Neighbours neighbours{};
		
		for(int j{}; j < 4; j++) {
			vec3i const offset{ neighbourDirs[j] };
			
			auto const neighbourIndex{ neighbourChunks[j].optChunk().get() };
			
			if(neighbourIndex >= 0) {
				neighbours[offset] = chunk::OptionalChunkIndex(neighbourIndex);
				chunks[neighbourIndex].neighbours()[chunk::Neighbours::mirror(offset)] = chunkIndex;
				chunks[neighbourIndex].status().setUpdateBlocks(true); //TODO: add separate flag when neighbour is updated
			}
			else neighbours[offset] = chunk::OptionalChunkIndex();
		}
		
		{
			vec3i const offset{ 0, 1, 0 };
			auto const neighbourIndex{ topNeighbourIndex.get() };
			
			if(neighbourIndex >= 0) {
				neighbours[offset] = chunk::OptionalChunkIndex(neighbourIndex);
				chunks[neighbourIndex].neighbours()[chunk::Neighbours::mirror(offset)] = chunkIndex;
			}
			else neighbours[offset] = chunk::OptionalChunkIndex();
		}
		
		neighbours_ = neighbours;
		
		fillChunkData(heights, chunk);
		
		if(emptyBefore && chunk.aabb().empty()) {
			chunk.skyLighting().fill(31u);
		}
		else {
			chunk.skyLighting().reset();
			emptyBefore = false;
			highestNotEmptyY = std::max(highestNotEmptyY, chunkPosition.y);
		}
		if(chunk.emitters().size() != 0) {
			highestWithBlockLighting = std::max(highestWithBlockLighting, chunkPosition.y);
			lowestWithBlockLighting  = std::min(lowestWithBlockLighting , chunkPosition.y);
		}
		
		if((y + 1)*units::blocksInChunkDim-1 >= minHeight) lowestNotFullY = std::min(lowestNotFullY, y);
		
		for(int i{}; i < 4; i ++) {
			vec3 const next{ vec3i{0,-1,0} };
			neighbourChunks[i].offset(next);
		}
		topNeighbourIndex = chunk::OptionalChunkIndex{ chunkIndex };
	}
	
	updateSkyLightingInChunks(chunks, vec3i{columnPosition.x, lowestNotFullY, columnPosition.y}, vec3i{columnPosition.x, highestNotEmptyY, columnPosition.y});
	setNeighboursLightingUpdate<SkyLightingConfig>(chunks, vec3i{columnPosition.x, -16, columnPosition.y}, vec3i{columnPosition.x, 15, columnPosition.y});
	
	updateBlockLightingInChunks(chunks, vec3i{columnPosition.x, -16, columnPosition.y}, vec3i{columnPosition.x, 15, columnPosition.y});
	if(highestWithBlockLighting >= lowestWithBlockLighting) setNeighboursLightingUpdate<BlocksLightingConfig>(
		chunks, 
		vec3i{columnPosition.x, highestWithBlockLighting+1, columnPosition.y},
		vec3i{columnPosition.x, lowestWithBlockLighting-1, columnPosition.y}
	);
}

static void updateChunks(chunk::Chunks &chunks) {
	if(numpad[1]) return;
	static std::vector<bool> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.resize(viewWidth*viewWidth);
	chunksPresent.assign(chunksPresent.size(), false);
	
	vec3i const playerChunk{ currentCoord().valAs<pos::Chunk>() };
	
	chunks.filterUsed([&](int chunkIndex) -> bool { //keep
			auto const relativeChunkPos = chunks.chunksPos[chunkIndex] - vec3i{ playerChunk.x, 0, playerChunk.z };
			auto const chunkInBounds{ relativeChunkPos.in(vec3i{-viewDistance, -16, -viewDistance}, vec3i{viewDistance, 15, viewDistance}).all() };
			auto const relativeChunkPosPositive = relativeChunkPos + vec3i{viewDistance, 16, viewDistance};
			auto const index{ relativeChunkPosPositive.x + relativeChunkPosPositive.z * viewWidth };
			if(chunkInBounds) {
				chunksPresent[index] = true;	
				return true;
			} 
			return false;
		}, 
		[&](int chunkIndex) -> void { //free chunk
			auto chunk{ chunks[chunkIndex] };
			chunks.chunksIndex_position.erase( chunk.position() );
			if(chunk.modified() && saveChunks) writeChunk(chunk);
			
			chunk.modified() = false;
			auto const &neighbours{ chunk.neighbours() };
			for(int i{}; i < chunk::Neighbours::neighboursCount; i++) {
				auto const &optNeighbour{ neighbours[i] };
				if(optNeighbour) {
					auto const neighbourIndex{ optNeighbour.get() };
					chunks[neighbourIndex].neighbours()[chunk::Neighbours::mirror(i)] = chunk::OptionalChunkIndex{};
					chunks[neighbourIndex].status().setUpdateBlocks(true); //TODO: add separate flag when neighbour is updated
				}
			}
		}
	);
	
	for(int i = 0; i < viewWidth; i ++) {
		for(int k = 0; k < viewWidth; k ++) {
			auto const index{ chunksPresent[i+k*viewWidth] };
			if(index == false) {//generate chunk
				auto const relativeChunksPos{ vec2i{i, k} - vec2i{viewDistance} };
				auto const chunksPos{ playerChunk.xz() + relativeChunksPos };
			
				genChunksColumnAt(chunks, chunksPos);
			}
		}	
	}
}

struct PosDir {
	vec3l start; //start inside chunk
	vec3l end;
	
	vec3i direction;
	vec3i chunk;
	
	PosDir(pos::Fractional const coord, vec3l const line): 
		start{ coord.valIn<pos::Chunk>() },
		end{ start + line },
		
		direction{ line.sign() },
		chunk{ coord.valAs<pos::Chunk>() }
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
	pos::Fractional at(vec3i inAxis, int64_t const coord) const { return { pos::Chunk{chunk} + pos::Fractional{part_at(inAxis, coord)} }; }
	
	friend std::ostream &operator<<(std::ostream &o, PosDir const v) {
		return o << "PosDir{" << v.start << ", " << v.end << ", " << v.direction << ", " << v.chunk << "}";
	}
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
			if(dir[i] >= 0) //round down
				return ((coord >> units::fracInCubeDimAsPow2) + 1) << units::fracInCubeDimAsPow2;
			else //round up
				return (-((-coord) >> units::fracInCubeDimAsPow2) - 1) << units::fracInCubeDimAsPow2;
		});
	};
	
	#define G(name) auto get_##name() const { return name ; }
	G(posDir)
	G(current)
	G(end)
	#undef G
};

static bool checkBlockIntersection(chunk::Chunks &chunks, pos::Fractional const coord1, pos::Fractional const coord2) {
	pos::Fractional const c1{ coord1.value().min(coord2.value()) };
	pos::Fractional const c2{ coord1.value().max(coord2.value()) };
	chunk::Move_to_neighbour_Chunk mtnChunk{chunks, c1.valAs<pos::Chunk>() };
	
	auto const c1c{ c1.valAs<pos::Cube>() };
	auto const c2c{ c2.valAs<pos::Cube>() };
	
	for(auto z{ c1c.z }; z <= c2c.z; z++) 
	for(auto y{ c1c.y }; y <= c2c.y; y++)
	for(auto x{ c1c.x }; x <= c2c.x; x++) {
		pos::Cube cubePos{{x, y, z}};
		
		auto const chunkIndex{ mtnChunk.move(cubePos.valAs<pos::Chunk>()).get() };
		if(chunkIndex == -1) continue;
		
		auto cube{ chunks[chunkIndex].data().cubeAt(cubePos.valIn<pos::Chunk>()) };
		
		if(cube.isSolid) return true;
	}
	
	return false;
}

static void updateCollision(chunk::Chunks &chunks, pos::Fractional &player, vec3d &playerForce, bool &isOnGround) {	
	//auto const playerChunk{ player.chunk() };
	
	vec3l playerPos{ player.value() };
	vec3l force{ pos::posToFracTrunk(playerForce).value() };
	
	vec3l maxPlayerPos{};
	
	vec3i dir{};
	vec3b positive_{};
	vec3b negative_{};
	
	vec3l playerMin{};
	vec3l playerMax{};
	
	vec3i min{};
	vec3i max{};
	
	chunk::Move_to_neighbour_Chunk chunk{ chunks, player.valAs<pos::Chunk>() };
	
	auto const updateBounds = [&]() {			
		dir = force.sign();
		positive_ = dir > vec3i(0);
		negative_ = dir < vec3i(0);
		
		maxPlayerPos = playerPos + force;
		
		/*static_*/assert(width_i % 2 == 0);
		playerMin = vec3l{ playerPos.x-width_i/2, playerPos.y         , playerPos.z-width_i/2 };
		playerMax = vec3l{ playerPos.x+width_i/2, playerPos.y+height_i, playerPos.z+width_i/2 };
		
		min = pos::Fractional(playerPos - vec3l{width_i/2,0,width_i/2}).valAs<pos::Cube>();
		max = pos::Fractional(playerPos + vec3l{width_i/2,height_i,width_i/2} - 1).valAs<pos::Cube>();
	};
	
	struct MovementResult {
		pos::Fractional coord;
		bool isCollision;
		bool continueMovement;
	};
	
	auto const moveAlong = [&](vec3b const axis, int64_t const axisPlayerOffset, vec3b const otherAxis1, vec3b const otherAxis2, bool const upStep) -> MovementResult {
		if(!( (otherAxis1 || otherAxis2).equal(!axis).all() )) {
			std::cout << "Error, axis not valid: " << axis << " - " << otherAxis1 << ' ' << otherAxis2 << '\n';
			assert(false);
		}
		
		auto const otherAxis{ otherAxis1 || otherAxis2 };
		
		auto const axisPositive{ positive_.dot(axis) };
		auto const axisNegative{ negative_.dot(axis) };
		auto const axisDir{ dir.dot(vec3i(axis)) };
		
		auto const axisPlayerMax{ playerMax.dot(vec3l(axis)) };
		auto const axisPlayerMin{ playerMax.dot(vec3l(axis)) };
		
		auto const axisPlayerPos{ playerPos.dot(vec3l(axis)) };
		auto const axisPlayerMaxPos{ maxPlayerPos.dot(vec3l(axis)) };
		
		auto const start{ units::Fractional{ axisPositive ? axisPlayerMax : axisPlayerMin }.valAs<units::Cube>() - axisNegative };
		auto const end{ units::Fractional{ axisPlayerMaxPos + axisPlayerOffset }.valAs<units::Cube>() };
		auto const count{ (end - start) * axisDir };
		
		
		for(int32_t a{}; a <= count; a++) {
			auto const axisCurCubeCoord{ start + a * axisDir };
			
			auto const axisNewCoord{ units::Cube{axisCurCubeCoord + axisNegative}.valAs<units::Fractional>() - axisPlayerOffset }; 
			pos::Fractional const newPos{ vec3l(axisNewCoord) * vec3l(axis) + vec3l(playerPos) * vec3l(otherAxis) };
				
			vec3l const upStepOffset{ vec3l{0, units::fracInCubeDim, 0} + vec3l(axis) * vec3l(axisDir) };
			pos::Fractional const upStepCoord{newPos + pos::Fractional{upStepOffset}};
			
			auto const upStepMin = (upStepCoord.value() - vec3l{width_i/2,0,width_i/2});
			auto const upStepMax = (upStepCoord.value() + vec3l{width_i/2,height_i,width_i/2} - 1);
			
			auto const upStepPossible{ upStep && !checkBlockIntersection(chunks, upStepMin, upStepMax)};
			
			auto const newCoordBigger{
				axisPositive ? (axisNewCoord >= axisPlayerPos) : (axisNewCoord <= axisPlayerPos)
			};
			
			for(auto o1{ min.dot(vec3i(otherAxis1)) }; o1 <= max.dot(vec3i(otherAxis1)); o1++)
			for(auto o2{ min.dot(vec3i(otherAxis2)) }; o2 <= max.dot(vec3i(otherAxis2)); o2++) {
				vec3i const cubeCoord{
					  vec3i(axis      ) * axisCurCubeCoord
					+ vec3i(otherAxis1) * o1
					+ vec3i(otherAxis2) * o2
				};
				
				auto const cubeLocalCoord{ 
					cubeCoord.applied([](auto const coord, auto i) -> int32_t {
						return int32_t(misc::mod<int64_t>(coord, units::cubesInBlockDim));
					})
				};
				
				pos::Fractional const coord{ pos::Cube(cubeCoord) };
						
				vec3i const blockChunk = coord.valAs<pos::Chunk>();
				
				auto const chunkIndex{ chunk.move(blockChunk).get() };
				if(chunkIndex == -1) return { newPos, false, false };
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ chunk::blockIndex(coord.as<pos::Block>().valIn<pos::Chunk>()) };
				auto const block{ chunkData[index] };
				
				if(block.id() != 0 && block.cube(cubeLocalCoord)) {
					if(newCoordBigger) {
						auto const diff{ playerPos.y - coord.value().y };
						if(upStepPossible && diff <= units::fracInCubeDim && diff >= 0) return { upStepCoord, false, true };
						else return { newPos, true, false };
					}
				}
			}
		}
		
		return { vec3l(axisPlayerMaxPos) * vec3l(axis) + vec3l(playerPos) * vec3l(otherAxis), false, false };
	};
	
	updateBounds();
		
	if(dir.y != 0) {
		MovementResult result;
		do { 
			result = moveAlong(vec3b{0,1,0}, (positive_.y ? height_i : 0), vec3b{0,0,1},vec3b{1,0,0}, false);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - playerPos) * vec3l{0,1,0};
			}
			else if(result.isCollision) {
				if(negative_.y) isOnGround = true;
				force = pos::posToFracTrunk(pos::fracToPos(force) * 0.8).value();
				force.y = 0;
			}
			
			playerPos = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}

	if(dir.x != 0) {
		MovementResult result;
		do { 
			result = moveAlong(vec3b{1,0,0}, width_i/2*dir.x, vec3b{0,0,1},vec3b{0,1,0}, isOnGround);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - playerPos) * vec3l{1,0,0};
			}
			else if(result.isCollision) { force.x = 0; }
			
			playerPos = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}
	
	if(dir.z != 0) {
		MovementResult result;
		do { 
			result = moveAlong(vec3b{0,0,1}, width_i/2*dir.z, vec3b{0,1,0},vec3b{1,0,0}, isOnGround);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - playerPos) * vec3l{0,0,1};
			}
			else if(result.isCollision) { force.z = 0; }
			
			playerPos = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}
	
	player = pos::Fractional{ playerPos };
	playerForce = pos::fracToPos(force) * (isOnGround ? vec3d{0.8,1,0.8} : vec3d{1});
}

bool checkCanPlaceBlock(vec3i const blockChunk, vec3i const blockCoord) {
	auto const relativeBlockPos{ pos::Chunk{blockChunk} + pos::Block{blockCoord} - currentCoord() };
	vec3l const blockStart{ relativeBlockPos.value() };
	vec3l const blockEnd{ (relativeBlockPos + pos::Block{1}).value() };
	
	/*static_*/assert(width_i % 2 == 0);
	
	return !(
		misc::intersectsX(0ll       , height_i ,  blockStart.y, blockEnd.y) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStart.x, blockEnd.x) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStart.z, blockEnd.z)
	);
}

struct BlockIntersection {
	chunk::Chunk chunk;
	int16_t blockIndex;
	uint8_t cubeIndex;
};

static std::optional<BlockIntersection> trace(chunk::Chunks &chunks, PosDir const pd) {
	DDA checkBlock{ pd };
		
	chunk::Move_to_neighbour_Chunk mtnChunk{ chunks, pd.chunk };
		
	vec3i intersectionAxis{0};
	for(int i = 0;; i++) {
		vec3l const intersection{ checkBlock.get_current() };
		
		pos::Cube const cubePos{
			pos::Chunk{ pd.chunk }.valAs<pos::Cube>()
			+ pos::Fractional(intersection).valAs<pos::Cube>()
			+ pd.direction.min(0) * intersectionAxis
		};
		
		auto const cubeLocalCoord{ 
			cubePos.value().applied([](auto const coord, auto i) -> int32_t {
				return int32_t(misc::mod<int64_t>(coord, units::cubesInBlockDim));
			})
		};
		
		auto const chunkIndex{ mtnChunk.move(cubePos.valAs<pos::Chunk>()).get() };
		if(chunkIndex == -1) break;
		
		auto const blockInChunkCoord{ cubePos.as<pos::Block>().valIn<pos::Chunk>() };
		auto const blockIndex{ chunk::blockIndex(blockInChunkCoord) };
		
		auto const chunk{ chunks[chunkIndex] };
		if(chunk.data().cubeAt(cubePos.valIn<pos::Chunk>()).isSolid) return {BlockIntersection{ chunk, blockIndex, chunk::Block::cubePosIndex(cubeLocalCoord) }};
		
		if(i >= 10000) {
			std::cout << __FILE__ << ':' << __LINE__ << " error: to many iterations!" << pd << '\n'; 
			break;
		}
		
		if(checkBlock.get_end()) {
			break;
		}
		
		intersectionAxis = vec3i(checkBlock.next());
	}

	return {};
}

static void markNeighboursIfAtBounds(chunk::Chunk chunk, vec3i const blockCoord) {//setNeedsUpdate for neighbouring chunks if their boundaries touch block at blockCoord
	auto &chunks{ chunk.chunks() };
	
	iterate3by3Volume([&](vec3i const dir, int const i) {
		if(dir == 0) return;
		auto const bounds{ dir.max(0).mix(vec3i{0}, vec3i{units::blocksInChunkDim-1}) };
		auto const dirMask{ !dir.equal(0) };
		auto const blockAtBounds{ (blockCoord.equal(bounds) && dirMask) == dirMask };
		
		if(blockAtBounds) {
			auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(dir) };
			if(neighbourChunkIndex.is()) {
				chunks[neighbourChunkIndex.get()].status().setUpdateBlocks(true); //add separate flag when neighbour is updated
				
			}
		}
	});
	
}

static void update(chunk::Chunks &chunks) {	
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

	if(diffBlockMs >= blockActionCD * 1000 && blockAction != BlockAction::NONE && !isSpectator) {
		bool isAction = false;

		auto const viewport{ currentCameraPos() };
		PosDir const pd{ PosDir(viewport, pos::posToFracTrunk(viewportCurrent.forwardDir() * 7).value()) };
		vec3i const dirSign{ pd.direction };
		DDA checkBlock{ pd };
	
		if(blockAction == BlockAction::BREAK) {
			auto optionalResult{ trace(chunks, pd) };
			
			if(optionalResult) {
				auto result{ *optionalResult };
				
				auto const blockCoord{ chunk::indexBlock(result.blockIndex) };
				auto chunk{ result.chunk };
				auto const &chunkData{ chunk.data() };
				auto &block{ chunk.data()[result.blockIndex] };
				auto const blockId{ block.id() };
				auto const emitter{ blockId == 14 || blockId == 13 };
				
				if(breakFullBlock) {
					block = chunk::Block::emptyBlock();
					
					if(emitter) {
						auto const cubeCoord{ blockCoord * units::cubesInBlockDim };
						SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, cubeCoord, cubeCoord + 1);
					}
					else for(int i{}; i < pos::cubesInBlockCount; i++) {
						auto const curCubeCoord{ blockCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
						AddLighting::fromCubeForcedFirst<BlocksLightingConfig>(chunk, curCubeCoord);
					}
					
					for(int i{}; i < pos::cubesInBlockCount; i++) {
						auto const curCubeCoord{ blockCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
						AddLighting::fromCubeForcedFirst<SkyLightingConfig>(chunk, curCubeCoord);
					}
				}
				else {
					auto const curCubeCoord{ blockCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(result.cubeIndex) };
					block = chunk::Block{ block.id(), uint8_t( block.cubes() & (~chunk::Block::blockCubeMask(result.cubeIndex)) ) };
					
					if(emitter) SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, curCubeCoord, curCubeCoord);
					else AddLighting::fromCubeForcedFirst<BlocksLightingConfig>(chunk, curCubeCoord);
					
					AddLighting::fromCubeForcedFirst<SkyLightingConfig>(chunk, curCubeCoord);
				}
				
				chunk.status().setUpdateBlocks(true);
				chunk.modified() = true;
				isAction = true;
				
				auto &aabb{ chunk.aabb() };
				vec3i const start_{ aabb.start() };
				vec3i const end_  { aabb.end  () };
				
				vec3i start{ units::blocksInChunkDim-1 };
				vec3i end  { 0 };
				
				for(int32_t x = start_.x; x <= end_.x; x++)
				for(int32_t y = start_.y; y <= end_.y; y++)
				for(int32_t z = start_.z; z <= end_.z; z++) {
					vec3i const blk{x, y, z};
					if(chunkData[chunk::blockIndex(blk)].id() != 0) {
						start = start.min(blk);
						end   = end  .max(blk);
					}
				}
				
				aabb = chunk::AABB(start, end);
				
				
				markNeighboursIfAtBounds(chunk, blockCoord);
			}
		}
		else {
			chunk::Move_to_neighbour_Chunk mtnChunk{ chunks, pd.chunk/*Coord*/ };
			for(int i = 0;; i++) {
				vec3b const intersectionAxis{ checkBlock.next() };
				vec3l const intersection{ checkBlock.get_current() };
				
				if(intersectionAxis == 0) break;
				
				auto const position{ 
					pos::Chunk{ pd.chunk } +
					pos::Cube{ 
						  pos::Fractional(intersection).valAs<pos::Cube>()
						+ pd.direction.min(0) * vec3i(intersectionAxis)
					} 
				};
				
				int chunkIndex{ mtnChunk.move(position.valAs<pos::Chunk>()).get() }; 
				if(chunkIndex == -1) break;
				
				auto const chunkData{ chunks.chunksData[chunkIndex] };
				auto const index{ chunk::blockIndex(position.as<pos::Block>().valIn<pos::Chunk>()) };
				auto const blockId{ chunkData[index].id() };
				
				if(blockId != 0) {
					auto const bc{ position - pos::Block{ dirSign * vec3i(intersectionAxis) } };
					vec3i const blockCoord = bc.as<pos::Block>().valIn<pos::Chunk>();
					vec3i const blockChunk = bc.valAs<pos::Chunk>();
			
					int chunkIndex{ mtnChunk.move(blockChunk).get() };
				
					auto const blockIndex{ chunk::blockIndex(blockCoord) };
					auto &block{ chunks.chunksData[chunkIndex][blockIndex] };
					
					
					if(checkCanPlaceBlock(blockChunk, blockCoord) && block.id() != 0) { std::cout << "!\n"; } 
					if(checkCanPlaceBlock(blockChunk, blockCoord) && block.id() == 0) {
						auto chunk{ chunks[chunkIndex] };
						block = chunk::Block::fullBlock(blockPlaceId);
						auto const emitter{ isBlockEmitter(blockPlaceId) };
						
						chunk.status().setUpdateBlocks(true);
						chunk.status().setLightingUpdated(true);
						chunk.modified() = true;
						isAction = true;
						
						auto &aabb{ chunk.aabb() };
						vec3i start{ aabb.start() };
						vec3i end  { aabb.end  () };
						
						start = start.min(blockCoord);
						end   = end  .max(blockCoord);
					
						aabb = chunk::AABB(start, end);
						
						markNeighboursIfAtBounds(chunk, blockCoord);
						
						SubtractLighting::inChunkCubes<SkyLightingConfig>(chunk, blockCoord*units::cubesInBlockDim, blockCoord*units::cubesInBlockDim + 1);
						SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, blockCoord*units::cubesInBlockDim, blockCoord*units::cubesInBlockDim + 1);						
						if(emitter) {
							for(int i{}; i < pos::cubesInBlockCount; i++) {
								auto const curCubeCoord{ blockCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
								BlocksLightingConfig::getLight(chunk, curCubeCoord) = 31u;		
							}
								
							for(int i{}; i < pos::cubesInBlockCount; i++) {
								auto const curCubeCoord{ blockCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
								AddLighting::fromCube<BlocksLightingConfig>(chunk, curCubeCoord);
							}
						}
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
	
	auto const curZoom{ currentZoom() };
		
	{
		double projection[3][3];
		viewportCurrent.localToGlobalSpace(&projection);
				
		auto const movement{ 
			vecMult( projection, vec3d(spectatorInput.movement).normalizedNonan() ) 
			* spectatorSpeed / curZoom
			* (shift ? 1.0*speedModifier : 1)
			* (ctrl  ? 1.0/speedModifier : 1)
		};
		
		spectatorCoord += pos::posToFrac(movement);
		spectatorInput = Input();
	}
	
	{
		static vec3d playerMovement{};
		
		playerMovement += ( 
			(     viewportCurrent.flatForwardDir() * playerInput.movement.z
				+ viewportCurrent.flatTopDir()     * playerInput.movement.y
				+ viewportCurrent.flatRightDir()   * playerInput.movement.x
			).normalizedNonan()
		    * playerSpeed
			* (shift ? 1.0*speedModifier : 1)
			* (ctrl  ? 1.0/speedModifier : 1)
		) * deltaTime; 
			
		if(diffPhysicsMs > fixedDeltaTime * 1000) {
			lastPhysicsUpdate = now; //several updates might be skipped

			if(!numpad[0]) {
				playerForce += vec3d{0,-1,0} * fixedDeltaTime; 
				if(isOnGround) {
					playerForce += (
						vec3d{0,1,0}*14*double(playerInput.jump)	
					) * fixedDeltaTime + playerMovement;
				}
				else {
					auto const movement{ playerMovement * 0.5 * fixedDeltaTime };
					playerForce = playerForce.applied([&](double const coord, auto const index) -> double { 
						return misc::clamp(coord + movement[index], fmin(coord, movement[index]), fmax(coord, movement[index]));
					});
				}

				isOnGround = false;
				updateCollision(chunks, playerCoord, playerForce, isOnGround);
			}
			
			playerMovement = 0;
		}
		
		playerInput = Input();
	}

	viewportDesired.rotation += deltaRotation * (2 * misc::pi) * (vec2d{ 0.8, -0.8 } / curZoom);
	viewportDesired.rotation.y = misc::clamp(viewportDesired.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
	deltaRotation = 0;
	
	if(isSmoothCamera) {
		viewportCurrent.rotation = vec2lerp( viewportCurrent.rotation, viewportDesired.rotation, vec2d(0.05) );
	}
	else {
		viewportCurrent.rotation = viewportDesired.rotation;
	}
	
	playerCamera.fov = misc::lerp( playerCamera.fov, 90.0 / 180 * misc::pi / curZoom, 0.1 );
	
	updateChunks(chunks);
	
	auto const diff{ pos::fracToPos(playerCoord+playerCameraOffset - playerCamPos) };
	playerCamPos = playerCamPos + pos::posToFrac(vec3lerp(vec3d{}, vec3d(diff), vec3d(0.4)));
}

void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity, 
							GLsizei length, const char *message, const void *userParam) {
	if(id == 131169 || id == 131185 || id == 131218 || id == 131204) return; 
	//std::cout << message << '\n';
}

void drawBlockHitbox(vec3f const blockRelativePos, float const size, float const (&toLoc4)[4][4]) {
	float const translation4[4][4] = {
			{ size, 0, 0, blockRelativePos.x },
			{ 0, size, 0, blockRelativePos.y },
			{ 0, 0, size, blockRelativePos.z },
			{ 0, 0, 0, 1                     },
	};			
	float playerToLocal[4][4];
	misc::matMult(toLoc4, translation4, &playerToLocal);
	
	glUseProgram(blockHitbox_p);
	glUniformMatrix4fv(blockHitboxModelMatrix_u, 1, GL_TRUE, &playerToLocal[0][0]);
	
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClear(GL_DEPTH_BUFFER_BIT);	
	
	glDrawArrays(GL_TRIANGLES, 0, 36);
	
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
}

int main(void) {	
    if (!glfwInit()) return -1;

    GLFWmonitor* monitor;
#ifdef FULLSCREEN
    monitor = glfwGetPrimaryMonitor();
#else
    monitor = NULL;
#endif // !FULLSCREEN
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);  
    window = glfwCreateWindow(windowSize.x, windowSize.y, "VMC", monitor, NULL);

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
	glfwSwapInterval( 0 );
	if(mouseCentered) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
        glfwTerminate();
        return -1;
    }

    fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));
	
	
	int flags; glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
	if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); 
		glDebugMessageCallback(glDebugOutput, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	} 

    //callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
	glfwSetCursorPos(window, 0, 0);
	cursor_position_callback(window, 0, 0);
	
	int constexpr charsCount{ 512*8 };
	GLuint fontVB;
	glGenBuffers(1, &fontVB);
	glBindBuffer(GL_ARRAY_BUFFER, fontVB);
	glBufferData(GL_ARRAY_BUFFER, sizeof(std::array<std::array<vec2f, 4>, charsCount>{}), NULL, GL_DYNAMIC_DRAW);
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
	
	updateChunks(chunks);
	
	reloadShaders();
	
	Counter<150> mc{};	
    while (!glfwWindowShouldClose(window)) {
		auto startFrame = std::chrono::steady_clock::now();
		
        auto const rightDir{ viewportCurrent.rightDir() };
        auto const topDir{ viewportCurrent.topDir() };
        auto const forwardDir{ viewportCurrent.forwardDir() };
		
		float toLoc[3][3];
		float toGlob[3][3];
		viewportCurrent.localToGlobalSpace(&toGlob);
		viewportCurrent.globalToLocalSpace(&toLoc);
		auto const cameraCoord{ currentCameraPos() };
		auto const cameraChunk{ cameraCoord.valAs<pos::Chunk>() };
		auto const cameraPosInChunk{ pos::fracToPos(cameraCoord.in<pos::Chunk>()) };
		
		float const toLoc4[4][4] = {
			{ toLoc[0][0], toLoc[0][1], toLoc[0][2], 0 },
			{ toLoc[1][0], toLoc[1][1], toLoc[1][2], 0 },
			{ toLoc[2][0], toLoc[2][1], toLoc[2][2], 0 },
			{ 0          , 0          , 0          , 1 },
		};
		
		float projection[4][4];
		currentCamera().projectionMatrix(&projection);
		
		glUseProgram(mainProgram);
		glUniformMatrix4fv(projection_u, 1, GL_TRUE, &projection[0][0]);
		
		glUseProgram(testProgram);
		glUniformMatrix4fv(tt_projection_u, 1, GL_TRUE, &projection[0][0]);
		
		glProgramUniformMatrix4fv(blockHitbox_p, blockHitboxProjection_u, 1, GL_TRUE, &projection[0][0]);
		
		//glClear(GL_COLOR_BUFFER_BIT);
		
		glEnable(GL_FRAMEBUFFER_SRGB); 
		glDisable(GL_DEPTH_TEST); 
		glDisable(GL_CULL_FACE); 
		
		glUseProgram(mainProgram);
		
        glUniform3f(rightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(topDir_u, topDir.x, topDir.y, topDir.z);
		
		auto const &currentCam{ currentCamera() };
		glUniform1f(near_u, currentCam.near);
        glUniform1f(far_u , currentCam.far );

		static double lastTime{};
		double curTime{ std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0 };
		
		static double offset{};
		if(numpad[2]) offset += curTime - lastTime;
		lastTime = curTime;
		glUniform1f(time_u, offset);
		
		if(testInfo) {
			std::cout.precision(17);
			std::cout << pos::fracToPos(currentCoord().in<pos::Chunk>()) << '\n';
			std::cout.precision(2);
			std::cout << currentCoord().as<pos::Block>().valIn<pos::Chunk>() << '\n';
			std::cout << currentCoord().valAs<pos::Chunk>() << '\n';
			std::cout << "----------------------------\n";
		}
		
		glUniform3i(playerChunk_u, cameraChunk.x, cameraChunk.y, cameraChunk.z);
		glUniform3f(playerInChunk_u, cameraPosInChunk.x, cameraPosInChunk.y, cameraPosInChunk.z);
		
		// -- // show updated chunks
		// -- // static std::vector<int> updatedChunks{};
		
		// -- // if(numpad[3]) { updatedChunks.clear(); numpad[3] = false; }
		
		auto const playerChunkCand{ chunk::Move_to_neighbour_Chunk(chunks, cameraChunk).optChunk().get() };
		if(playerChunkCand != -1) {
			int playerChunkIndex = playerChunkCand;
			glUniform1i(startChunkIndex_u, playerChunkIndex);
			
			vec3f const playerRelativePos( pos::fracToPos(playerCoord - pos::Chunk{cameraChunk}) );
			glUniform3f(playerRelativePosition_u, playerRelativePos.x, playerRelativePos.y, playerRelativePos.z);
			
			glUniform1i(drawPlayer_u, isSpectator);
			
			int updatedCount{};
			for(auto const chunkIndex : chunks.usedChunks()) {
				bool showUpdated = false;
				const bool updated{ updateChunk(chunks[chunkIndex], cameraChunk, false && updatedCount > 2, *&showUpdated) };
				if(showUpdated) {
					// -- // updatedChunks.push_back(chunkIndex); 
				}
				if(updated) updatedCount++; 
			}
			if(updatedCount > 0) std::cout << updatedCount << '\n';

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
		}
		else glClear(GL_COLOR_BUFFER_BIT);
		
		// -- // 
		/*{
			for(auto const chunkIndex : updatedChunks) {
				auto const chunk{ chunks[chunkIndex] };
				auto const chunkCoord{ chunk.position() }; 

				vec3f const relativeChunkPos( pos::fracToPos(pos::Chunk{chunkCoord} - cameraCoord) );
				drawBlockHitbox(relativeChunkPos, units::fracToPos(units::Chunk{1}), toLoc4);
			}
		}*/
		
		
		{
			PosDir const pd{ PosDir(cameraCoord, pos::posToFracTrunk(forwardDir * 7).value()) };
			auto const optionalResult{ trace(chunks, pd) };
				
			if(optionalResult) {
				auto const result{ *optionalResult };
				auto const chunk { result.chunk };
				
				auto const blockCoord{ pos::Chunk{chunk.position()} + pos::Block{chunk::indexBlock(result.blockIndex)} };
				
				auto const blockRelativePos{ 
					vec3f(pos::fracToPos(blockCoord - cameraCoord)) +  
					(breakFullBlock ? vec3f{0} : (vec3f{chunk::Block::cubeIndexPos(result.cubeIndex)} * 0.5))
				};
				float const size{ breakFullBlock ? 1.0f : 0.5f };

				drawBlockHitbox(blockRelativePos, size, toLoc4);
			}
		}
		
		
		{
			glUseProgram(currentBlockProgram);
			glUniform1ui(cb_blockIndex_u, blockPlaceId);
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
	
		{ //font
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			
			vec2f const textPos(10, 10);
			    
			std::stringstream ss{};
			ss << std::fixed;
			
			if(debugInfo) {
				/* COMMIT_HASH COMMIT_NAME COMMIT_BRANCH COMMIT_DATE */
				
				#if defined COMMIT_HASH && defined COMMIT_BRANCH && defined defined && defined COMMIT_DATE
					ss << COMMIT_HASH << " - " << COMMIT_BRANCH << " : " << COMMIT_NAME << " (" << COMMIT_DATE << ")" << '\n';
				#else
					ss << "No information about build version\n";
				#endif
				
				PosDir const pd{ PosDir(cameraCoord, pos::posToFracTrunk(viewportCurrent.forwardDir() * 7).value()) };
				auto const optionalResult{ trace(chunks, pd) };
				if(optionalResult) {
					auto const result{ *optionalResult };
					auto const chunk { result.chunk };
					
					auto const blockInChunkCoord{ chunk::indexBlock(result.blockIndex) };
					auto const cubeInBlockCoord{ chunk::Block::cubeIndexPos(result.cubeIndex) };
					
					ss.precision(1);
					auto const block{ chunk.data()[blockInChunkCoord] };
					ss << "looking at: chunk=" << chunk.position() << " inside chunk=" << (vec3f(blockInChunkCoord*units::cubesInBlockDim + cubeInBlockCoord)/units::cubesInBlockDim)  << " : block id=" << block.id() << '\n';
				};
				
				ss.precision(4);
				ss << "camera in: chunk=" << currentCoord().valAs<pos::Chunk>() << " inside chunk=" << pos::fracToPos(currentCoord().valIn<pos::Chunk>()) << '\n';
				if(playerChunkCand != -1) ss << "emitters count in camera chunk=" << chunks[playerChunkCand].emitters().size() << '\n';
				if(playerChunkCand != -1) {
					ss << "neighbouring emitters in camera chunk=" ;
					//for(int i{}; i < chunks[playerChunkCand].emitters().size(); i++)
					//	ss << chunks[playerChunkCand].emitters()(i) << '\n';
					for(int i{}; i < chunk::Chunk3x3BlocksList::capacity; i++)
						ss << chunks[playerChunkCand].neighbouringEmitters()(i) << '\n';
				}
				ss << "camera forward=" << forwardDir << '\n';
			}
			ss.precision(1);
			ss << (1000000.0 / mc.mean()) << '(' << (1000000.0 / mc.max()) << ')' << "FPS";
			
			std::string const text{ ss.str() };
	
			auto const textCount{ std::min<unsigned long long>(text.size(), charsCount) };
			
			std::array<std::array<vec2f, 4>, charsCount> data;
			
			vec2f const startPoint(textPos.x / windowSize_d.x * 2 - 1, 1 - textPos.y / windowSize_d.y * 2);
			
			auto const lineH = font.base();
			auto const lineHeight = font.lineHeight();
			float const scale = 5;
			
			vec2f currentPoint(startPoint);
			for(uint64_t i{}; i != textCount; i++) {
				auto const ch{ text[i] };
				if(ch == '\n') {
					currentPoint = vec2f(startPoint.x, currentPoint.y - lineHeight / scale);
					continue;
				}
				auto const &fc{ font[ch] };
				
				auto const charOffset{ vec2f(fc.xOffset, -fc.yOffset - lineHeight) / scale };
				if(ch != ' ') data[i] = {
					//pos
					currentPoint + vec2f(0, -fc.height + lineH) / scale + charOffset,
					currentPoint + vec2f(fc.width*aspect, 0 + lineH) / scale + charOffset, 
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
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, textCount);
			glBindVertexArray(0);
			
			glDisable(GL_BLEND);
		}
		
		
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
		
        update(chunks);
		
		auto const dTime{ std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startFrame).count() };
		mc.add(dTime);
		deltaTime = double(dTime) / 1000000.0;
    }
    glfwTerminate();
	
	{
		if(saveChunks) {
			int count = 0;
			
			for(auto const chunkIndex : chunks.usedChunks()) {
				auto chunk{ chunks[chunkIndex] };
				if(chunk.modified()) {
					writeChunk(chunk);
					count++;
				}
			}
			
			std::cout << "saved chunks count: " << count;
		}
	}
	
    return 0;
}