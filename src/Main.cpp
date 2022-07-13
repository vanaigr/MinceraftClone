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
#include"LightingPropagation.h"
#include"Lighting.h"
#include"AO.h"
#include"Area.h"
#include"BlockProperties.h"
#include"BlocksData.h"
#include"Liquid.h"
#include"Trace.h"
#include"Physics.h"

#include"Config.h"
#include"Font.h"
#include"ShaderLoader.h"
#include"Read.h"
#include"Misc.h"
#include"PerlinNoise.h"
#include"Counter.h"
#include"SaveBMP.h"

#include <string.h>
#include <math.h>
#include<iostream>
#include<chrono>
#include<optional>
#include<vector>
#include<array>
#include<limits>
#include<sstream>
#include<fstream>

//https://learnopengl.com/In-Practice/Debugging
GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR)
    {
        std::string error;
        switch (errorCode)
        {
            case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        std::cout << error << " | " << file << " (" << line << ")" << std::endl;
    }
    return errorCode;
}
#define ce glCheckError_(__FILE__, __LINE__);

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2i windowSize{ 1920, 1080 };
#else
static const vec2i windowSize{ 1280, 720 };
#endif // FULLSCREEN
static const vec2d windowSize_d{ windowSize.convertedTo<double>() };
static double const aspect{ windowSize_d.y / windowSize_d.x };

static bool lockFramerate = false;


static double       deltaTime{ 16.0/1000.0 };
static double const fixedDeltaTime{ 16.0/1000.0 };

GLFWwindow* window;

static int  viewDistance = 3;
static bool loadChunks = false, saveChunks = false;
static vec2d mouseSensitivity{ 0.8, -0.8 };
static int chunkUpdatesPerFrame = 5;


static double const playerHeight{ 1.95 };
static double const playerWidth{ 0.6 };

static int64_t const height_i{ units::posToFracRAway(playerHeight).value() };
static int64_t const width_i{ units::posToFracRAway(playerWidth).value() };
// /*static_*/assert(dimensions->x % 2 == 0);
// /*static_*/assert(dimensions->z % 2 == 0);

vec3l const playerOffsetMin{ -width_i/2, 0       , -width_i/2 };
vec3l const playerOffsetMax{  width_i/2, height_i,  width_i/2 };

static double speedModifier = 2.5;
static double playerSpeed{ 2.7 };
static double spectatorSpeed{ 6 };

static vec3d playerForce{};
static bool isOnGround{false};

static vec3l const playerCameraOffset{ 0, units::posToFrac(playerHeight*0.85).value(), 0 };
static pos::Fractional playerCoord{ pos::posToFrac(vec3d{0.5,12.001,0.5}) };
static pos::Fractional playerCamPos{playerCoord + playerCameraOffset}; //position is lerp'ed

static pos::Fractional spectatorCoord{ playerCoord };

static Camera desiredPlayerCamera{
	aspect,
	90.0 / 180.0 * misc::pi,
	0.001,
	800
};
static Camera playerCamera{ desiredPlayerCamera };


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

static const Font font{ "./assets/font.txt" };

enum Textures : GLuint {
	atlas_it = 0,
	font_it,
	noise_it,
	screenshotColor_it,
	
	texturesCount
};
static GLuint textures[texturesCount];
static GLuint const		&atlas_t           = textures[atlas_it], 
						&font_t            = textures[font_it], 
						&noise_t           = textures[noise_it],
						&screenshotColor_t = textures[screenshotColor_it];

enum SSBOs : GLuint {
	chunksIndices_b = 0,
	chunksBlocks_b,
	chunksLiquid_b,
	chunksMesh_b,
	chunksBounds_b,
	chunksAO_b,
	chunksLighting_b,
	chunksEmittersGPU_b,
	atlasDescription_b,
	
	ssbosCount
};

static GLuint ssbos[ssbosCount];
static GLuint const	
	&chunksIndices_ssbo       = ssbos[chunksIndices_b],
	&chunksBlocks_ssbo        = ssbos[chunksBlocks_b], 
	&chunksLiquid_ssbo        = ssbos[chunksLiquid_b],	
	&chunksMesh_ssbo          = ssbos[chunksMesh_b],
	&chunksBounds_ssbo        = ssbos[chunksBounds_b], 
	&chunksAO_ssbo            = ssbos[chunksAO_b],
	&chunksLighting_ssbo      = ssbos[chunksLighting_b],
	&chunksEmittersGPU_ssbo   = ssbos[chunksEmittersGPU_b],
	&atlasDescription_ssbo    = ssbos[atlasDescription_b];

static GLuint mainProgram = 0;
  static GLuint windowSize_u;
  static GLuint rightDir_u;
  static GLuint topDir_u;
  static GLuint near_u, far_u;
  static GLuint playerRelativePosition_u, drawPlayer_u;
  static GLuint startChunkIndex_u;
  static GLuint time_u;
  static GLuint projection_u, toLocal_matrix_u;
  static GLuint startCoord_u;
  //static GLuint playerChunk_u, playerInChunk_u;

static GLuint fontProgram;

static GLuint testProgram;
	static GLuint tt_projection_u, tt_toLocal_u;
	
static GLuint currentBlockProgram;
  static GLuint cb_blockIndex_u;

static GLuint blockHitbox_p;
  static GLuint blockHitboxProjection_u, blockHitboxModelMatrix_u;

static GLuint screenshot_fb;
static vec2i screenshotSize{ windowSize };
bool takeScreenshot;

static chunk::Chunks chunks{};
static ChunksLiquidCubes chunksLiquid{ chunks };


enum class Key : uint8_t { RELEASE = GLFW_RELEASE, PRESS = GLFW_PRESS, REPEAT = GLFW_REPEAT, NOT_PRESSED };
static_assert(GLFW_RELEASE >= 0 && GLFW_RELEASE < 256 && GLFW_PRESS >= 0 && GLFW_PRESS < 256 && GLFW_REPEAT >= 0 && GLFW_REPEAT < 256);

static bool shift{ false }, ctrl{ false };
static Key keys[GLFW_KEY_LAST+1];

static void reloadConfig();
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
	else if(key == GLFW_KEY_F6 && action == GLFW_PRESS)
		reloadConfig();	
	else if(key == GLFW_KEY_F4 && action == GLFW_PRESS)
		takeScreenshot = true;
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
		deltaRotation += (mousePos - pmousePos) / windowSize_d * mouseSensitivity;
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
	blockPlaceId = 1+misc::mod(blockPlaceId-1 + int(yoffset), 15);
}


static bool checkChunkInView(vec3i const coord) {
	return coord.clamp(-viewDistance, +viewDistance) == coord;
}

struct GPUChunksIndex {
	chunk::Chunks::index_t lastIndex;
	std::vector<chunk::Chunks::index_t> vacant;
	
	chunk::Chunks::index_t reserve() {
		if(vacant.empty()) return ++lastIndex; //never 0
		else {
			auto const index{ vacant.back() };
			vacant.pop_back();
			return index;
		}
	}
	
	void recycle(chunk::Chunks::index_t const index) {
		if(index == 0) return;
		assert(std::find(vacant.begin(), vacant.end(), index) == vacant.end());
		assert(index > 0);
		vacant.push_back(index);
	}
	
	void reset() { // = GPUChunksIndex{}, but vector keeps its capacity
		lastIndex = 0;
		vacant.clear();
	}
} static gpuChunksIndex{}; 

struct PackedAABB { //used in main.frag
	static constexpr int64_t cd = units::blocksInChunkDim-1;
	static_assert( cd*cd*cd * cd*cd*cd < (1ll << 32), "two block indices must fit into 32 bits" );
private:
	uint32_t data;
public:
	PackedAABB() = default;
	PackedAABB(vec3i const start, vec3i const end) : data{ 
		uint32_t(uint16_t(chunk::blockCoordToIndex(pBlock{start})))
		| (uint32_t(uint16_t(chunk::blockCoordToIndex(pBlock{end}))) << 16) 
	} {}
	PackedAABB(Area const a) : PackedAABB{ a.first, a.last } {}
	
	//constexpr uint32_t getData() const { return data; }
	//constexpr vec3i start() const { return chunk::blockIndexToCoord(int16_t(data&0xffff)).val(); }
	//constexpr vec3i end() const { return chunk::blockIndexToCoord(int16_t(data>>16)).val(); } 
	//constexpr vec3i onePastEnd() const { return end() + 1; }
	//constexpr bool empty() const { return (end() < start()).any(); };
	};

void gpuBuffersReseted() {
	auto const renderDiameter{ viewDistance*2+1 };
	auto const gridSize{ renderDiameter * renderDiameter * renderDiameter };
	auto const gpuChunksCount{ gridSize + 1/*zero'th chunk*/ };
	
	gpuChunksIndex.reset();
	for(auto &it : chunks.chunksGPUIndex) it = chunk::Chunks::index_t{};
	
	for(auto &status : chunks.chunksStatus) {
		status.setEverythingUpdated();
	}
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksIndices_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gridSize * sizeof(chunk::Chunks::index_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksIndices_b, chunksIndices_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlocks_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * pos::blocksInChunkCount * sizeof(chunk::Block::id_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksBlocks_b, chunksBlocks_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLiquid_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkLiquid), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksLiquid_b, chunksLiquid_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);		
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksMesh_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::BlocksData), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksMesh_b, chunksMesh_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksBounds_b, chunksBounds_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	static_assert(sizeof(chunk::ChunkAO) == sizeof(uint8_t) * chunk::ChunkAO::size);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksAO_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::ChunkAO), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksAO_b, chunksAO_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	static_assert(sizeof(chunk::ChunkLighting) == sizeof(uint8_t) * chunk::ChunkLighting::size);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLighting_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * 2 * sizeof(chunk::ChunkLighting), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksLighting_b, chunksLighting_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);		
	
	static_assert(sizeof(chunk::Chunk3x3BlocksList) == sizeof(uint32_t) * 16);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * sizeof(chunk::Chunk3x3BlocksList), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_b, chunksEmittersGPU_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	//note: glClearBufferSubData(...);
	
	static chunk::Block::id_t blocksId[pos::blocksInChunkCount] = {};
	static chunk::ChunkLiquid liquid{};
	static chunk::BlocksData blocksData{};
	static PackedAABB aabb{ vec3i{units::blocksInChunkDim-1}, vec3i{0} };
	static chunk::ChunkAO ao{};
	static chunk::ChunkLighting lighting[2] = { chunk::ChunkLighting{chunk::ChunkLighting::maxValue}, chunk::ChunkLighting{chunk::ChunkLighting::maxValue} };
	static chunk::Chunk3x3BlocksList nEmitters{};
	static chunk::Chunks::index_t gpuIndex{};
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksAO_ssbo);

	glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * sizeof(chunk::ChunkAO), sizeof(chunk::ChunkAO), &ao);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlocks_ssbo); 
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(blocksId) * gpuIndex, sizeof(blocksId), &blocksId);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);		
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLiquid_ssbo); 
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(liquid) * gpuIndex, sizeof(liquid), &liquid);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksMesh_ssbo); 
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(blocksData) * gpuIndex, sizeof(blocksData), &blocksData);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo); 
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * gpuIndex, sizeof(uint32_t), &aabb);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLighting_ssbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * 2 * sizeof(chunk::ChunkLighting), 2 * sizeof(chunk::ChunkLighting), &lighting);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_ssbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * sizeof(chunk::Chunk3x3BlocksList), sizeof(chunk::Chunk3x3BlocksList), &nEmitters);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		
}

void printLinkErrors(GLuint const prog, char const *const name) {
	GLint progErrorStatus;
	glGetProgramiv(prog, GL_LINK_STATUS, &progErrorStatus);
	if(progErrorStatus) return;
	
	std::cout << "Program status: " << progErrorStatus << '\n';
	int length;
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
	GLchar *msg = new GLchar[length + 1];
	glGetProgramInfoLog(prog, length, &length, msg);
	std::cout << "Program error:\n" << msg;
	delete[] msg;
} 

static void reloadConfig() {
	Config cfg{};
		cfg.viewDistance =  viewDistance;
		cfg.loadChunks = saveChunks;
		cfg.saveChunks = saveChunks;
		cfg.playerCameraFovDeg = playerCamera.fov / misc::pi * 180.0;
		cfg.mouseSensitivity = mouseSensitivity;
		cfg.chunkUpdatesPerFrame = chunkUpdatesPerFrame;
		cfg.lockFramerate = lockFramerate;
		cfg.screenshotSize = screenshotSize;
		
	parseConfigFromFile(cfg);
	
	bool shouldReloadShaders = viewDistance != cfg.viewDistance || !(screenshotSize == cfg.screenshotSize);
	
	viewDistance = cfg.viewDistance;
	loadChunks = cfg.loadChunks;
	saveChunks = cfg.saveChunks;
	desiredPlayerCamera.fov = cfg.playerCameraFovDeg / 180.0 * misc::pi;
	mouseSensitivity = cfg.mouseSensitivity;
	playerCamera = desiredPlayerCamera;
	chunkUpdatesPerFrame = cfg.chunkUpdatesPerFrame;
	lockFramerate = cfg.lockFramerate;
	screenshotSize = cfg.screenshotSize;
	
	if(shouldReloadShaders) reloadShaders();
}

static void reloadShaders() {
	glDeleteTextures(texturesCount, &textures[0]);
	glGenTextures   (texturesCount, &textures[0]);
	
	glDeleteBuffers(ssbosCount, &ssbos[0]);
	glGenBuffers   (ssbosCount, &ssbos[0]);
	
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
	
	{ //screenshot framebuffer
		glActiveTexture(GL_TEXTURE0 + screenshotColor_it);
		glBindTexture(GL_TEXTURE_2D, screenshotColor_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, screenshotSize.x, screenshotSize.y, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		  
		glDeleteFramebuffers(1, &screenshot_fb);
		glGenFramebuffers(1, &screenshot_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, screenshot_fb);
		  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenshotColor_t, 0);
		  
		  GLenum status;
		  if ((status = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE) {
		  	fprintf(stderr, "screenshot framebuffer: error %u", status);
		  }
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	
	{
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
				c(18, 0), c(18, 0), c(18, 0), c(0, 1), //water
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
		
		windowSize_u = glGetUniformLocation(mainProgram, "windowSize");
		glUniform2ui(windowSize_u, windowSize.x, windowSize.y);
		
		glUniform1f(glGetUniformLocation(mainProgram, "playerWidth" ), playerWidth );
		glUniform1f(glGetUniformLocation(mainProgram, "playerHeight"), playerHeight);
		
		time_u   = glGetUniformLocation(mainProgram, "time");

		rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
		topDir_u = glGetUniformLocation(mainProgram, "topDir");
		near_u = glGetUniformLocation(mainProgram, "near");
		far_u = glGetUniformLocation(mainProgram, "far");
		projection_u = glGetUniformLocation(mainProgram, "projection");
		toLocal_matrix_u = glGetUniformLocation(mainProgram, "toLocal");
	
		//playerChunk_u = glGetUniformLocation(mainProgram, "playerChunk");
		//playerInChunk_u = glGetUniformLocation(mainProgram, "playerInChunk");
		//startChunkIndex_u = glGetUniformLocation(mainProgram, "startChunkIndex");
		startCoord_u = glGetUniformLocation(mainProgram, "startCoord");
		
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
		glUniform1i(glGetUniformLocation(mainProgram, "viewDistance"), viewDistance);
		
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksIndices"), chunksIndices_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksBlocks"), chunksBlocks_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksLiquid"), chunksLiquid_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksMesh"), chunksMesh_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "AtlasDescription"), atlasDescription_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksBounds"), chunksBounds_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksAO"), chunksAO_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksLighting"), chunksLighting_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "ChunksNeighbourngEmitters"), chunksEmittersGPU_b);
		
		gpuBuffersReseted();
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

inline void updateNeighbouringEmitters(chunk::Chunk chunk) {
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
		if(neighbourIndex.is()) {
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

static void updateEmitters(chunk::Chunk chunk) {
	auto &blocks{ chunk.data() };
	auto &aabb{ chunk.aabb() };
	auto &emitters{ chunk.emitters() };
	emitters.clear();
	iterateArea(aabb, [&](vec3i const blockInChunkCoord) {
		if(isBlockEmitter(blocks[blockInChunkCoord].id())) emitters.add(blockInChunkCoord);
	});
}

static void setChunksUpdateNeighbouringEmitters(chunk::Chunk chunk) {
	auto &chunks{ chunk.chunks() };
	iterate3by3Volume([&](vec3i const dir, int const i) {
		auto const neighbourIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(dir).get() };
		if(neighbourIndex != -1) {
			chunks[neighbourIndex].status().setUpdateNeighbouringEmitters(true);
		}
	});
}

static bool updateChunk(chunk::Chunk chunk, vec3i const cameraChunkCoord, bool const maxUpdated) {
	static constexpr bool updateChunkDebug = false;
	
	auto &status{ chunk.status() };
			
	if(numpad[4]) {
		updateBlocksDataInArea(chunk, pBlock{0}, pBlock{units::blocksInChunkDim-1});
		status.setBlocksUpdated(true);
	}
	auto const &chunkCoord{ chunk.position() };
	auto const chunkIndex{ chunk.chunkIndex() };
	
	auto const localChunkCoord{ chunkCoord - cameraChunkCoord };
	auto const chunkInView{ checkChunkInView(localChunkCoord) };
	
	if(!chunkInView) return false;
	
	auto &gpuIndex{ chunk.gpuIndex() };
	
	auto const isnLoaded{ gpuIndex == 0 };
	auto const shouldUpdate{ status.needsUpdate() || status.isInvalidated() || isnLoaded };
	
	
	if(shouldUpdate && (cameraChunkCoord == chunkCoord || !maxUpdated)) {
		if(!gpuIndex) gpuIndex = gpuChunksIndex.reserve();
		
		if(status.isUpdateAO()) {
			auto const &aabb{ chunk.aabb() };
			updateAOInArea(chunk, pBlock{aabb.first}.as<pCube>(), pBlock{aabb.last+1} - pCube{1});

			status.setUpdateAO(false);
			status.setAOUpdated(true);
		}
		if(isnLoaded || status.isAOUpdated()) {
			auto const &ao{ chunk.ao() };
			
			static_assert(sizeof(chunk::ChunkAO) == sizeof(uint8_t) * chunk::ChunkAO::size);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksAO_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * sizeof(chunk::ChunkAO), sizeof(chunk::ChunkAO), &ao);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
			status.setAOUpdated(false);
		}
		
		if(isnLoaded || status.isBlocksUpdated()) {
			if constexpr(updateChunkDebug) std::cout << "b ";
			
			PackedAABB const aabbData{ chunk.aabb() };
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBounds_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * gpuIndex, sizeof(uint32_t), &aabbData);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
			auto const &chunkBlocks{ chunk.blocks() };
			auto const &chunkLiquid{ chunk.liquid() };
			auto const &blocksData{ chunk.blocksData() };
			
			static chunk::Block::id_t blocksId[pos::blocksInChunkCount];
			
			for(int i{}; i < pos::blocksInChunkCount; i++) {
				pBlock const blockCoord{ chunk::indexBlock(i) };
				
				blocksId[i] = chunkBlocks[blockCoord].id();
			}
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksBlocks_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(blocksId) * gpuIndex, sizeof(blocksId), &blocksId);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLiquid_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(chunkLiquid) * gpuIndex, sizeof(chunkLiquid), &chunkLiquid);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksMesh_ssbo); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(blocksData) * gpuIndex, sizeof(blocksData), &blocksData);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
			
			status.setBlocksUpdated(false);
		}					
	
		if(status.isUpdateLightingAdd()) {
			auto &chunks{ chunk.chunks() };
			
			fillEmittersBlockLighting(chunk);
			
			updateLightingInChunks<SkyLightingConfig>   (chunks, chunkCoord, chunkCoord);
			updateLightingInChunks<BlocksLightingConfig>(chunks, chunkCoord, chunkCoord);
			
			setNeighboursLightingUpdate<SkyLightingConfig, BlocksLightingConfig>(chunks, chunkCoord, chunkCoord);
			
			status.setUpdateLightingAdd(false);
			status.setLightingUpdated(true);
		}
		if(isnLoaded || status.isLightingUpdated()) {
			if constexpr(updateChunkDebug) std::cout << "l ";
			auto const &skyLighting{ chunk.skyLighting() };
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLighting_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * 2 * sizeof(chunk::ChunkLighting), sizeof(chunk::ChunkLighting), &skyLighting);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);			
			
			auto const &blockLighting{ chunk.blockLighting() };
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksLighting_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * 2 * sizeof(chunk::ChunkLighting) + sizeof(chunk::ChunkLighting), sizeof(chunk::ChunkLighting), &blockLighting);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			status.setLightingUpdated(false);
		}
		
		if(status.isUpdateNeighbouringEmitters()) {
			updateNeighbouringEmitters(chunk);
			
			status.setUpdateNeighbouringEmitters(false);
			status.setNeighbouringEmittersUpdated(true);
		}
		if(isnLoaded || status.isNeighbouringEmittersUpdated()) {	
			if constexpr(updateChunkDebug) std::cout << "e ";
			auto const &emittersGPU{ chunk.neighbouringEmitters() };
			
			static_assert(sizeof(chunk::Chunk3x3BlocksList) == sizeof(uint32_t)*16);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksEmittersGPU_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, gpuIndex * sizeof(chunk::Chunk3x3BlocksList), sizeof(chunk::Chunk3x3BlocksList), &emittersGPU);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			status.setNeighbouringEmittersUpdated(false);
		}
		
		return true;
	}
	
	return false;
}


std::string chunkFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save/" << pos << ".cnk";
	return ss.str();
}

std::string chunkNewFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save2/" << pos << ".cnk2";
	return ss.str();
}

std::string chunkColumnFilename(vec2i const columnChunkCoordXY) {
	std::stringstream ss{};
	ss << "./save/" << columnChunkCoordXY << ".ccm";
	return ss.str();
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

bool tryReadChunk(chunk::Chunk chunk, BlockBounds &bounds) {
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
				bounds.first = bounds.first.min(blockCoord);
				bounds.last  = bounds.last .max(blockCoord);
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
				bounds.first = bounds.first.min(blockCoord);
				bounds.last  = bounds.last .max(blockCoord);
			}
			
			if(isBlockEmitter(block)) emitters.add(blockCoord);
		}

		return true;
	}
	
	
	return false;
}

static void genTrees(chunk::Chunk chunk, BlockBounds &bounds) {	
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
						bounds.first = bounds.first.min(blk);
						bounds.last  = bounds.last .max(blk);
					}
				}
			}
		}
	}
}

static void genChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk, BlockBounds &bounds) {
	auto const &pos{ chunk.position() };
	auto &blocks{ chunk.data() };
	auto &liquid{ chunk.liquid() };
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
			
			bounds.first = bounds.first.min(blockCoord);
			bounds.last  = bounds.last .max(blockCoord);
			
			blocks[blockCoord] = chunk::Block::fullBlock(block);
		}
		else {
			blocks[blockCoord] = chunk::Block::emptyBlock();
			if(pos.y * units::blocksInChunkDim + y == 7) {
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					pCube const cubeLocalCoord{ chunk::Block::cubeIndexPos(cubeIndex) };
					auto const cubeCoord{ pBlock{blockCoord} + cubeLocalCoord }; 
					
					if(cubeLocalCoord.val().y == units::cubesInBlockDim-1)
						liquid[cubeCoord] = chunk::LiquidCube{15, 254u};
					else 
						liquid[cubeCoord] = chunk::LiquidCube{15, 255u};
				}
			}
			if(pos.y * units::blocksInChunkDim + y < 7) {
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					auto const cubeCoord{ pBlock{blockCoord} + pCube{ chunk::Block::cubeIndexPos(cubeIndex) } }; 
					liquid[cubeCoord] = chunk::LiquidCube{15, 255u};
				}
			}
		}
		
		
		if(isBlockEmitter(blocks[blockCoord].id())) emitters.add(blockCoord);
	}
		
	genTrees(chunk, bounds);	
}

static void fillChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk, BlockBounds &bounds) {
	if(loadChunks  && tryReadChunk(         chunk, bounds)) chunk.modified() = false;
	else {            genChunkData(heights, chunk, bounds); chunk.modified() = true ; }
	
}

static void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition) {
	//generate heights for each block column in the chunk column
	double heights[units::blocksInChunkDim * units::blocksInChunkDim];
	auto minHeight{ std::numeric_limits<double>::infinity() };
	for(int z{}; z < units::blocksInChunkDim; z++) 
	for(int x{}; x < units::blocksInChunkDim; x++) {
		auto const height{  heightAt(vec2i{columnPosition.x,columnPosition.y}, vec2i{x,z}) };
		heights[z* units::blocksInChunkDim + x] = height;
		minHeight = std::min(minHeight, height);
	}

	constexpr int neighbourDirsCount = 8; //horizontal neighbours only
	vec3i const neighbourDirs[] = { 
		vec3i{-1,0,-1}, vec3i{-1,0,0}, vec3i{-1,0,+1}, vec3i{0,0,+1}, vec3i{+1,0,+1}, vec3i{+1,0,0}, vec3i{+1,0,-1}, vec3i{0,0,-1}
	};
	chunk::Move_to_neighbour_Chunk neighbourChunks[neighbourDirsCount] = { 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}
	};
	neighbourChunks[0] = {chunks, vec3i{columnPosition.x, chunkColumnChunkYMax, columnPosition.y} + neighbourDirs[0]};
	
	for(int i{1}; i < neighbourDirsCount; i++) {
		auto mtn{ chunk::Move_to_neighbour_Chunk{neighbourChunks[i-1]} };
		mtn.move(vec3i{columnPosition.x, chunkColumnChunkYMax, columnPosition.y} + neighbourDirs[i]);
		neighbourChunks[i] = mtn;
	}
	
	chunk::OptionalChunkIndex topNeighbourIndex{};
	
	int chunkIndices[chunksCoumnChunksCount];
	
	auto lowestEmptyY{ chunkColumnChunkYMax + 1 };
	auto lowestNotFullY  { chunkColumnChunkYMax + 1 };
	auto emptyBefore{ true };

	auto lowestWithBlockLighting { chunkColumnChunkYMax + 1 };
	auto highestWithBlockLighting{ chunkColumnChunkYMin - 1 };
	
	struct ChunkIndexAndNeighbours{ 
		int chunkIndex; 
		chunk::OptionalChunkIndex neighbours[neighbourDirsCount];
	};
	ChunkIndexAndNeighbours chunkIndicesWithNeighbours[chunksCoumnChunksCount];
	
	for(auto y { chunkColumnChunkYMax }; y >= chunkColumnChunkYMin; y--) {
		auto const usedIndex{ chunks.reserve() };
		
		auto  const chunkIndex{ chunks.usedChunks()[usedIndex] };
		vec3i const chunkPosition{ columnPosition.x, y, columnPosition.y };
		
		chunks.chunksIndex_position[chunkPosition] = chunkIndex;
		chunkIndices[y - chunkColumnChunkYMin] = chunkIndex;
		
		auto chunk{ chunks[chunkIndex] };
		
		chunk.position() = chunkPosition;
		chunk.status() = chunk::ChunkStatus{};
		chunk.status().setEverythingUpdated();
		chunk.status().setUpdateAO(true);
		chunk.status().setUpdateNeighbouringEmitters(true);
		chunk.gpuIndex() = chunk::Chunks::index_t{};
		chunk.liquid().reset();
		chunk.ao().reset();
		chunk.blockLighting().reset();
		chunk.emitters().clear();
		chunk.neighbouringEmitters().clear();
		
		chunk.neighbours() = [&] {
			chunk::Neighbours neighbours{};
			
			for(int j{}; j < neighbourDirsCount; j++) {
				vec3i const offset{ neighbourDirs[j] };
				if(!chunk::Neighbours::checkDirValid(offset)) continue;
					
				auto const neighbourIndex{ neighbourChunks[j].optChunk().get() };
				
				if(neighbourIndex >= 0) {
					auto neighbourChunk{ chunks[neighbourIndex] };
					neighbours[offset] = chunk::OptionalChunkIndex(neighbourIndex);
					neighbourChunk.neighbours()[chunk::Neighbours::mirror(offset)] = chunkIndex;
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
			
			return neighbours;
		}();
		
		BlockBounds chunkBounds = BlockBounds::emptyChunk();
		fillChunkData(heights, chunk, chunkBounds);
		
		fillEmittersBlockLighting(chunk);
		
		chunk.aabb() = { chunkBounds.first, chunkBounds.last };
		
		if(emptyBefore && chunk.aabb().isEmpty()) {
			chunk.skyLighting().fill(chunk::ChunkLighting::maxValue);
			lowestEmptyY = misc::min(lowestEmptyY, chunkPosition.y);
		}
		else {
			chunk.skyLighting().reset();
			emptyBefore = false;
		}
		if(chunk.emitters().size() != 0) {
			highestWithBlockLighting = std::max(highestWithBlockLighting, chunkPosition.y);
			lowestWithBlockLighting  = std::min(lowestWithBlockLighting , chunkPosition.y);
		}
		
		if((y + 1)*units::blocksInChunkDim-1 >= minHeight) lowestNotFullY = std::min(lowestNotFullY, y);
		
		chunkIndicesWithNeighbours[y - chunkColumnChunkYMin].chunkIndex = chunkIndex;
		
		//updateNeighbourChunks and move to next
		for(int i{}; i < neighbourDirsCount; i ++) {
			vec3i const offset{ neighbourDirs[i] };
			auto const optChunkIndex{ neighbourChunks[i].optChunk() };
			chunkIndicesWithNeighbours[y - chunkColumnChunkYMin].neighbours[i] = optChunkIndex;
			
			vec3 const next{ vec3i{0,-1,0} };
			neighbourChunks[i].offset(next);
		}
		topNeighbourIndex = chunk::OptionalChunkIndex{ chunkIndex };
	}
	
	for(auto const &[chunkIndex, neighbours] : chunkIndicesWithNeighbours) {		
		auto chunk{ chunks[chunkIndex] };
		
		auto const &aabb{ chunk.aabb() };
		
		auto const updateBlocksNoNeighbours{ !aabb.isEmpty() };
		auto const areaUpdateNoNeighbours{ intersectAreas3i(
			Area{ vec3i{-1} , vec3i{units::blocksInChunkDim-1 + 1} },
			{ aabb.first - 1, aabb.last + 1 }
		) };
		
		//AO is updated later in updateChunk()
		
		if(updateBlocksNoNeighbours) { //blocks with no neighbours
			updateBlocksDataInArea(chunk, pBlock{aabb.first}, pBlock{aabb.last});
			//chunk.status().setBlocksUpdated(true); //already set
		}
		
		for(int i{}; i < neighbourDirsCount; i++) {
			auto const &neighbourIndex{ neighbours[i] };
			if(!neighbourIndex.is()) continue;
			
			auto const offset{ neighbourDirs[i] };	
			auto neighbourChunk{ chunks[neighbourIndex.get()] };
			auto const &neighbourAabb{ neighbourChunk.aabb() };
			auto const neighbourFirst{ neighbourAabb.first };
			auto const neighbourLast { neighbourAabb.last  };

			if(!neighbourChunk.status().isUpdateAO()) { //AO
				auto const updatedAreaCubes{ intersectAreas3i(
					{ vec3i{0} - offset*units::cubesInChunkDim, vec3i{units::cubesInChunkDim} - offset*units::cubesInChunkDim },
					{ neighbourFirst * units::cubesInBlockDim, (neighbourLast+1) * units::cubesInBlockDim - 1 })
				};
				
				if(!updatedAreaCubes.isEmpty()) {
					updateAOInArea(neighbourChunk, pCube{updatedAreaCubes.first}, pCube{updatedAreaCubes.last});
					neighbourChunk.status().setAOUpdated(true);
				}
			}
			
			if(updateBlocksNoNeighbours) { //blocks with no neighbours
				Area const updatedAreaBlocks_{ 
					areaUpdateNoNeighbours.first - offset*units::blocksInChunkDim,
					areaUpdateNoNeighbours.last  - offset*units::blocksInChunkDim
				};
				auto const updatedAreaBlocks{ intersectAreas3i(
					updatedAreaBlocks_,
					{ 0, units::blocksInChunkDim-1 }
				) };
				
				if(!updatedAreaBlocks.isEmpty()) {
					updateBlocksDataInArea(neighbourChunk, pBlock{updatedAreaBlocks.first}, pBlock{updatedAreaBlocks.last});
					neighbourChunk.status().setBlocksUpdated(true);
				}
			}
			
			neighbourChunk.status().setUpdateNeighbouringEmitters(true);
		}
	}
	
	calculateLighting(chunks, chunkIndices, columnPosition, lowestEmptyY, lowestNotFullY);
}

static void updateChunks(chunk::Chunks &chunks) {
	if(numpad[1]) return;
	
	static std::vector<bool> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.clear();
	chunksPresent.resize(viewWidth*viewWidth);
	
	vec3i const currentChunk{ currentCoord().valAs<pos::Chunk>() };
	chunks.filterUsed([&](int chunkIndex) -> bool { //should keep
			auto const relativeChunkPos = chunks.chunksPos[chunkIndex] - vec3i{ currentChunk.x, 0, currentChunk.z };
			auto const chunkInBounds{ relativeChunkPos.in(vec3i{-viewDistance, -16, -viewDistance}, vec3i{viewDistance, 15, viewDistance}).all() };
			auto const relativeChunkPosPositive = relativeChunkPos + vec3i{viewDistance, 16, viewDistance};
			auto const index{ relativeChunkPosPositive.x + relativeChunkPosPositive.z * viewWidth };
			if(chunkInBounds) {
				chunksPresent[index] = true;	
				return true;
			} 
			return false;
		}, 
		[&](int const chunkIndex) -> void { //free the chunk
			auto chunk{ chunks[chunkIndex] };
			gpuChunksIndex.recycle(chunk.gpuIndex());
			chunks.chunksIndex_position.erase( chunk.position() );
			if(chunk.modified() && saveChunks) writeChunk(chunk);
			
			auto const &neighbours{ chunk.neighbours() };
			for(int i{}; i < chunk::Neighbours::neighboursCount; i++) {
				auto const &optNeighbour{ neighbours[i] };
				if(optNeighbour.is()) {
					auto const neighbourIndex{ optNeighbour.get() };
					chunks[neighbourIndex].neighbours()[chunk::Neighbours::mirror(i)] = chunk::OptionalChunkIndex{};
					
					/*
						note: we could call updateAOInArea, updateBlocksDataInArea for this chunk
						but it is not critical
					*/
				}
			}
		}
	);

	for(int i = 0; i < viewWidth; i ++) {
		for(int k = 0; k < viewWidth; k ++) {
			auto const index{ chunksPresent[i+k*viewWidth] };
			if(index == false) {//generate chunk
				auto const relativeChunksPos{ vec2i{i, k} - vec2i{viewDistance} };
				auto const chunksPos{ currentChunk.xz() + relativeChunksPos };
			
				genChunksColumnAt(chunks, chunksPos);
			}
		}	
	}
}

bool checkCanPlaceBlock(pBlock const blockPos) {
	auto const relativeBlockPos{ blockPos - playerCoord };
	vec3l const blockStart{ relativeBlockPos.value() };
	vec3l const blockEnd{ (relativeBlockPos + pos::Block{1}).value() };
	
	/*static_*/assert(width_i % 2 == 0);
	
	return !(
		misc::intersectsX(0ll       , height_i ,  blockStart.y, blockEnd.y) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStart.x, blockEnd.x) &&
		misc::intersectsX(-width_i/2, width_i/2,  blockStart.z, blockEnd.z)
	);
}

void updateAOandBlocksWithoutNeighbours(chunk::Chunk chunk, pCube const first, pCube const last) {
	updateAOInArea(chunk, first.val(), (last + pCube{1}).val() );
	updateBlocksDataInArea(chunk, first.as<pBlock>() - pBlock{1}, last.as<pBlock>() + pBlock{1});
}

bool performBlockAction() {
	auto const viewport{ currentCameraPos() };
	PosDir const pd{ PosDir(viewport, pos::posToFracTrunk(viewportCurrent.forwardDir() * 7).value()) };
	vec3i const dirSign{ pd.direction };
	
	if(blockAction == BlockAction::BREAK) {
		auto optionalResult{ trace(chunks, pd) };
		
		if(!optionalResult) return false;
		
		auto result{ *optionalResult };
		
		pCube const cubeInChunkPos{ result.cubeInChunkCoord };
		pBlock const blockInChunkPos{ cubeInChunkPos.as<pBlock>() };
		pCube const cubeInBlockPos{ cubeInChunkPos.in<pBlock>() };
		auto chunk{ result.chunk };
		auto &block{ chunk.data()[blockInChunkPos] };
		auto const blockId{ block.id() };
		auto const emitter{ isBlockEmitter(blockId) };
		
		auto const blockCoord{ blockInChunkPos.valAs<pBlock>() };
		
		
		pCube const first{ breakFullBlock ? blockInChunkPos.as<pCube>()             : cubeInChunkPos };
		pCube const last { breakFullBlock ? blockInChunkPos + pBlock{1} - pCube{1}  : cubeInChunkPos };
		
		iterateArea(first.val()-1, last.val()+1, [&](pCube const neighbourCubeCoord) { //not a very effective method
			auto const neighbourChunkIndex{ chunk::MovingChunk{chunk}.offseted(neighbourCubeCoord.valAs<pChunk>()).getIndex() };
			if(!neighbourChunkIndex.is()) return;
			auto neighbourInChunkCoord{ neighbourCubeCoord.in<pChunk>() };
			
			chunksLiquid.add({neighbourChunkIndex.get(), chunk::cubeCoordToIndex(neighbourInChunkCoord)});
		});
		
		if(breakFullBlock) {
			block = chunk::Block::emptyBlock();
			
			if(emitter) {
				//auto const cubeCoord{ blockCoord * units::cubesInBlockDim };
				SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, first.valAs<pCube>(), last.valAs<pCube>());
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
			auto const curCubeCoord{ cubeInChunkPos.val() };
			block = chunk::Block{ block.id(), uint8_t( block.cubes() & (~chunk::Block::blockCubeMask(cubeInBlockPos.val())) ) };
			
			if(emitter) SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, curCubeCoord, curCubeCoord);
			else AddLighting::fromCubeForcedFirst<BlocksLightingConfig>(chunk, curCubeCoord);
			
			AddLighting::fromCubeForcedFirst<SkyLightingConfig>(chunk, curCubeCoord);
		}
		
		auto const wholeBlockRemoved{ block.isEmpty() };
		
		chunk.modified() = true;
		
		updateAOandBlocksWithoutNeighbours(chunk, first, last);
		
		auto &aabb{ chunk.aabb() };
		
		vec3i start{ units::blocksInChunkDim-1 };
		vec3i end  { 0 };
		
		auto const &blocksData{ chunk.blocksData() };
		iterateArea(aabb, [&](vec3i const blockCoord) {
			if(blocksData[pBlock{blockCoord}].isEmpty()) return;
			start = start.min(blockCoord);
			end   = end  .max(blockCoord);
		});
		
		aabb = { start, end };
	
		if(emitter && wholeBlockRemoved) {
			chunk.emitters().remove(blockCoord);
			setChunksUpdateNeighbouringEmitters(chunk);
		}
		
		iterate3by3Volume([&](vec3i const dir, int const index) {
			auto const chunkOffset{ (blockInChunkPos + pBlock{dir}).valAs<pChunk>() };
			auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(chunkOffset) };
			if(!chunkIndex.is()) return;
			auto &chunkStatus{ chunks[chunkIndex.get()].status() };
			
			chunkStatus.setAOUpdated(true);
			chunkStatus.setBlocksUpdated(true);
		});
	}
	else {
		auto optionalResult{ trace(chunks, pd) };
		
		if(!optionalResult) return false;
		
		auto result{ *optionalResult };
		auto const intersectionAxis{ result.intersectionAxis };
		auto startChunk{ result.chunk };
		auto const normal{ -dirSign * vec3i{intersectionAxis} };
		
		auto const cubePosInStartChunk { result.cubeInChunkCoord + pCube{normal} };
		
		auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{startChunk}.moveToNeighbour(cubePosInStartChunk.valAs<pChunk>()) };
		if(!chunkIndex.is()) return false;
		auto chunk{ chunks[chunkIndex.get()] };
		auto &liquid{ chunk.liquid() };
		
		pChunk const chunkPos{ chunk.position() };
		
		auto const cubeInChunkPos{ cubePosInStartChunk.in<pChunk>() };
		auto const blockInChunkPos{ cubeInChunkPos.as<pBlock>() };
		auto const blockInChunkCoord{ blockInChunkPos.val() };
		
		pCube const first{ blockInChunkPos                        };
		pCube const last { blockInChunkPos + pBlock{1} - pCube{1} };
		
		if(blockPlaceId == 15) { //water
			auto &block{ chunk.data()[blockInChunkCoord] };
				
			if(block.id() == 0) {
				
				liquid[cubeInChunkPos] = chunk::LiquidCube{ uint16_t(blockPlaceId), 255 };
				chunksLiquid.add({chunk.chunkIndex(), chunk::cubeCoordToIndex(cubeInChunkPos)});
				chunk.status().setBlocksUpdated(true);
			}
			else return false;
		}
		else {
			auto &block{ chunk.data()[blockInChunkCoord] };
				
			if(checkCanPlaceBlock(chunkPos + blockInChunkPos) && block.id() == 0) {
				block = chunk::Block::fullBlock(blockPlaceId);
				
				for(int i{}; i < pos::cubesInBlockCount; i++) {
					auto const curCubeCoord{ blockInChunkPos + pCube{chunk::Block::cubeIndexPos(i)} };	
					liquid[curCubeCoord] = chunk::LiquidCube{};
				}
				
				auto const blockId{ block.id() };
				auto const emitter{ isBlockEmitter(blockId) };
				
				chunk.modified() = true;
				
				auto &aabb{ chunk.aabb() };
				aabb = aabb + Area{blockInChunkCoord, blockInChunkCoord};
				
				SubtractLighting::inChunkCubes<SkyLightingConfig>(chunk, blockInChunkCoord*units::cubesInBlockDim, blockInChunkCoord*units::cubesInBlockDim + 1);
				SubtractLighting::inChunkCubes<BlocksLightingConfig>(chunk, blockInChunkCoord*units::cubesInBlockDim, blockInChunkCoord*units::cubesInBlockDim + 1);						
				if(emitter) {
					for(int i{}; i < pos::cubesInBlockCount; i++) {
						auto const curCubeCoord{ blockInChunkCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
						BlocksLightingConfig::getLight(chunk, curCubeCoord) = chunk::ChunkLighting::maxValue;		
					}
						
					for(int i{}; i < pos::cubesInBlockCount; i++) {
						auto const curCubeCoord{ blockInChunkCoord * units::cubesInBlockDim + chunk::Block::cubeIndexPos(i) };
						AddLighting::fromCube<BlocksLightingConfig>(chunk, curCubeCoord);
					}
				}
				
				if(emitter) {
					chunk.emitters().add(blockInChunkCoord);
					setChunksUpdateNeighbouringEmitters(chunk);
				}
				updateAOandBlocksWithoutNeighbours(chunk, first, last);
				
				iterate3by3Volume([&](vec3i const dir, int const index) {
					auto const chunkOffset{ (blockInChunkPos + pBlock{dir}).valAs<pChunk>() };
					auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(chunkOffset) };
					if(!chunkIndex.is()) return;
					auto &chunkStatus{ chunks[chunkIndex.get()].status() };
				
					chunkStatus.setAOUpdated(true);
					chunkStatus.setBlocksUpdated(true);
				});
			}
			else return false;
		}
	}
	
	return true;
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

	if(diffBlockMs >= blockActionCD * 1000 && blockAction != BlockAction::NONE) {
		bool const isPerformed = performBlockAction();
		if(isPerformed) lastBlockUpdate = now;
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
			* deltaTime
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
			lastPhysicsUpdate += std::chrono::microseconds{int64_t(fixedDeltaTime*1000000.0)};

			if(!numpad[0]) {
				playerForce += vec3d{0,-1,0} * fixedDeltaTime; 
				if(isOnGround) {
					playerForce += (
						vec3d{0,1,0}*14*double(playerInput.jump)	
					) * fixedDeltaTime + playerMovement;
				}
				else {
					auto const movement{ playerMovement * 0.5 };
					playerForce = playerForce.applied([&](double const coord, auto const index) -> double { 
						return misc::clamp(coord + movement[index], fmin(coord, movement[index]), fmax(coord, movement[index]));
					});
				}

				isOnGround = false;
				updateCollision(chunks, playerCoord, playerOffsetMin, playerOffsetMax, playerForce, isOnGround);
			}
			
			playerMovement = 0;
		}
		
		playerInput = Input();
	}

	viewportDesired.rotation += deltaRotation * (2 * misc::pi) / curZoom;
	viewportDesired.rotation.y = misc::clamp(viewportDesired.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
	deltaRotation = 0;
	
	if(isSmoothCamera) {
		viewportCurrent.rotation = vec2lerp( viewportCurrent.rotation, viewportDesired.rotation, vec2d(0.05) );
	}
	else {
		viewportCurrent.rotation = viewportDesired.rotation;
	}
	
	playerCamera.fov = misc::lerp( playerCamera.fov, desiredPlayerCamera.fov / curZoom, 0.1 );
	
	updateChunks(chunks);
	if(!numpad[0]) chunksLiquid.update();
	
	auto const diff{ pos::fracToPos(playerCoord+playerCameraOffset - playerCamPos) };
	playerCamPos = playerCamPos + pos::posToFrac(vec3lerp(vec3d{}, vec3d(diff), vec3d(0.4)));
	
}

void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity, 
							GLsizei length, const char *message, const void *userParam) {
	if(id == 131169 || id == 131185 || id == 131218 || id == 131204) return; 
	//std::cout << message << '\n';
}

template<typename T>
void printBinary(std::ostream &o, T const it_) {
	static constexpr auto bits = sizeof(T) * 8;
	
	typename std::make_unsigned<T>::type it{ it_ };
	o << "0b";
	for(int i{bits-1}; i >= 0; i--) {
		auto const bit{ (it / (1 << i)) % 2 };
		o << (bit ? '1' : '0');
        if((i % 8) == 0 && i != 0 && i != bits-1) o << '\'';
	}
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
	auto const startupTime{ std::chrono::steady_clock::now() };
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

	reloadConfig();
	updateChunks(chunks);
	reloadShaders();
	
    auto const completionTime = std::chrono::steady_clock::now();
	std::cout << "Time to start (ms): " << ( double(std::chrono::duration_cast<std::chrono::microseconds>(completionTime - startupTime).count()) / 1000.0 ) << '\n';
	
	Counter<150> mc{};
	Counter<150> timeToTrace{};
	bool firstFrame = true;
    while (!glfwWindowShouldClose(window)) {
		auto startFrame = std::chrono::steady_clock::now();
		
		if(lockFramerate) glfwSwapInterval(1);
		else glfwSwapInterval(0);
		
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
		double curTime{ std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - completionTime).count() / 1000.0 };
		
		static double offset{};
		if(numpad[2]) offset += curTime - lastTime;
		lastTime = curTime;
		glUniform1f(time_u, offset / 1000.0);
		
		if(testInfo) {
			std::cout.precision(17);
			std::cout << pos::fracToPos(currentCoord().in<pos::Chunk>()) << '\n';
			std::cout.precision(2);
			std::cout << currentCoord().as<pos::Block>().valIn<pos::Chunk>() << '\n';
			std::cout << currentCoord().valAs<pos::Chunk>() << '\n';
			std::cout << "----------------------------\n";
		}
		
		static pChunk lastCameraChunkCoord{};
		
		if(!numpad[1]) {
			lastCameraChunkCoord = pChunk{cameraChunk};
		}
		auto const startCoord{ pos::fracToPos(cameraCoord - lastCameraChunkCoord + pChunk{viewDistance}) };
		
		glUniform3f(startCoord_u, startCoord.x, startCoord.y, startCoord.z);
		
		{			
			auto const playerChunkCand{ chunk::Move_to_neighbour_Chunk(chunks, lastCameraChunkCoord.val()).optChunk().get() };
			vec3f const playerRelativePos( pos::fracToPos(playerCoord - lastCameraChunkCoord + pChunk{viewDistance}) );
			glUniform3f(playerRelativePosition_u, playerRelativePos.x, playerRelativePos.y, playerRelativePos.z);
			
			glUniform1i(drawPlayer_u, isSpectator);
			 
			//reset gpu indices that are not visible 
			for(auto const chunkIndex : chunks.usedChunks()) { 
				auto chunk{ chunks[chunkIndex] };
				auto const chunkCoord{ chunk.position() };
				auto const localChunkCoord{ chunkCoord - lastCameraChunkCoord.val() };
				auto const chunkInView{ checkChunkInView(localChunkCoord) };
				
				auto &gpuIndex{ chunk.gpuIndex() };
				if(!chunkInView) {
					gpuChunksIndex.recycle(gpuIndex);
					gpuIndex = chunk::Chunks::index_t{};
				}
			}
			
			//update chunks and construct gpu indices grid
			int updatedCount{};

			auto const renderDiameter{ viewDistance*2+1 };
			auto const gpuChunksCount{ renderDiameter*renderDiameter*renderDiameter };
			
			static std::vector<chunk::Chunks::index_t> indices{};
			indices.resize(gpuChunksCount);
			int i = 0;
			/*(clear, reserve, push_back) can be used instead of (resize, [i++])*/
			
			iterateAllChunks(
				chunks[playerChunkCand], 
				lastCameraChunkCoord - pChunk{viewDistance}, lastCameraChunkCoord + pChunk{viewDistance}, 
				[&](chunk::Chunks::index_t const chunkIndex, pChunk const coord) {
					if(chunkIndex == -1) { indices[i++] = 0; return; }
					
					auto chunk{ chunks[chunkIndex] };
					
					const bool updated{ updateChunk(chunk, lastCameraChunkCoord.val(), updatedCount >= chunkUpdatesPerFrame) };
					if(updated) updatedCount++; 
				
					indices[i++] = chunk.gpuIndex();	
			});
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksIndices_ssbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpuChunksCount * sizeof(chunk::Chunks::index_t), &indices[0]);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	

			if(numpad[3]) glFinish();
			auto const startTraceTime{ std::chrono::steady_clock::now() };
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
			if(numpad[3]) glFinish();
			auto const endTraceTime{ std::chrono::steady_clock::now() };
			
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
			
			if(takeScreenshot) {
				takeScreenshot = false;
				
				glBindFramebuffer(GL_FRAMEBUFFER, screenshot_fb);
				glUniform2ui(windowSize_u, screenshotSize.x, screenshotSize.y);
				glViewport(0, 0, screenshotSize.x, screenshotSize.y);
				
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
				glFinish();
				
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glUniform2ui(windowSize_u, windowSize.x, windowSize.y);
				glViewport(0, 0, windowSize.x, windowSize.y);
				
				std::unique_ptr<uint8_t[]> data{ new uint8_t[3 * screenshotSize.x * screenshotSize.y] };
				
				glActiveTexture(GL_TEXTURE0 + screenshotColor_it);
				glGetTexImage(GL_TEXTURE_2D, 0, GL_BGR, GL_UNSIGNED_BYTE, data.get());
				
				generateBitmapImage(data.get(), screenshotSize.y, screenshotSize.x, "screenshot.bmp");
			}
			
			auto const diffMs{ std::chrono::duration_cast<std::chrono::microseconds>(endTraceTime - startTraceTime).count() / 1000.0 };
			timeToTrace.add(diffMs);
		}

		{
			PosDir const pd{ PosDir(cameraCoord, pos::posToFracTrunk(forwardDir * 7).value()) };
			auto const optionalResult{ trace(chunks, pd) };
				
			if(optionalResult) {
				auto const result{ *optionalResult };
				auto const chunk { result.chunk };
				
				auto const blockRelativePos{ vec3f(pos::fracToPos(
					(breakFullBlock ? result.cubePos.as<pBlock>().as<pCube>() : result.cubePos) - cameraCoord
				)) };
				float const size{ breakFullBlock ? 1.0f : (1.0f / units::cubesInBlockDim) };

				drawBlockHitbox(blockRelativePos, size, toLoc4);
			}
		}
		
		{
			glUseProgram(currentBlockProgram);
			glUniform1ui(cb_blockIndex_u, blockPlaceId);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		
		glDisable(GL_FRAMEBUFFER_SRGB); 

		{ //font
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			
			vec2f const textPos(10, 10);
			    
			std::stringstream ss{};
			ss << std::fixed;
			
			if(debugInfo) {
				/* COMMIT_HASH COMMIT_NAME COMMIT_BRANCH COMMIT_DATE */
				
				#if defined COMMIT_HASH && defined COMMIT_BRANCH && defined COMMIT_NAME && defined COMMIT_DATE
					ss << COMMIT_HASH << " - " << COMMIT_BRANCH << " : " << COMMIT_NAME << " (" << COMMIT_DATE << ")" << '\n';
				#else
					ss << "No information about build version\n";
				#endif
				
				PosDir const pd{ PosDir(cameraCoord, pos::posToFracTrunk(viewportCurrent.forwardDir() * 7).value()) };
				auto const optionalResult{ trace(chunks, pd) };
				if(optionalResult) {
					auto const result{ *optionalResult };
					auto const chunk { result.chunk };
					
					auto const cubeInChunkPos{ result.cubeInChunkCoord };
					auto const blockInChunkCoord{ cubeInChunkPos.as<pBlock>() };
					auto const cubeInBlockCoord { cubeInChunkPos.in<pBlock>() };
					
					
					ss.precision(1);
					ss << "looking at: chunk=" << chunk.position() << " inside chunk=" << vec3f(pos::fracToPos(cubeInChunkPos.as<pFrac>())) << '\n';
					ss << "chunk aabb: " << chunk.aabb().first << ' ' << chunk.aabb().last << '\n';
					
					{
						auto const block{ chunk.data()[blockInChunkCoord] };
						auto const liquid{ chunk.liquid()[cubeInChunkPos] };
						ss << "sky lighting: " << int(chunk.skyLighting()[cubeInChunkPos.val()]) 
						   << " block lighting: " << int(chunk.blockLighting()[cubeInChunkPos.val()]) << '\n';
						ss << "block id=" << int{block.id()} << "; liquid id=" << int{liquid.id} << " liquid level=" << int{liquid.level} << '\n';
					}
					
					{
						auto const intersectionAxis{ result.intersectionAxis };
						vec3i const dirSign{ pd.direction };
						auto const normal{ -dirSign * vec3i{intersectionAxis} };
						
						auto const beforePosInStartChunk { cubeInChunkPos + pCube{normal} };
						
						auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(beforePosInStartChunk.valAs<pChunk>()) };
						if(chunkIndex.is()) {
							auto chunk{ chunks[chunkIndex.get()] };
							
							auto const cubeInChunkPos{ beforePosInStartChunk.in<pChunk>() };
							auto const blockInChunkPos{ cubeInChunkPos.as<pBlock>() };
							
							auto const blockData{ chunk.blocksData()[blockInChunkPos] };
							auto const block{ chunk.data()[blockInChunkPos] };
							auto const liquid{ chunk.liquid()[cubeInChunkPos] };
							ss << "cube before: \n";
							ss << "sky lighting: " << int(chunk.skyLighting()[cubeInChunkPos.val()]) 
							<< " block lighting: " << int(chunk.blockLighting()[cubeInChunkPos.val()]) << '\n';
							ss << "block id=" << int{block.id()} << "; liquid id=" << int{liquid.id} << " liquid level=" << int{liquid.level} << '\n';
							ss << "solid cubes=";
							printBinary(ss, blockData.solidCubes); ss << ' ';
							ss << "liquid cubes=";
							printBinary(ss, blockData.liquidCubes); ss << '\n';
						}
					}
				};
				
				ss.precision(4);
				ss << "camera in: chunk=" << currentCoord().valAs<pos::Chunk>() << " inside chunk=" << pos::fracToPos(currentCoord().valIn<pos::Chunk>()) << '\n';
				ss << "camera forward=" << forwardDir << '\n';
				ss << "frame time=" << (mc.mean() / 1000.0) << "ms\n";
				if(numpad[3]) {
					ss << "trace time=" << timeToTrace.mean() << "ms\n";
				}
				else {
					ss << "press numpad 3 to see time to render the world\n";
					ss << "(this may affect the performance)\n";
				}
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
		
		if(firstFrame) {
			firstFrame = false;
			
			auto const completionTime = std::chrono::steady_clock::now();
			std::cout << "Time to draw first frame (ms): "
				<< ( double(std::chrono::duration_cast<std::chrono::microseconds>(completionTime - startupTime).count()) / 1000.0 )
				<< '\n';
		}
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