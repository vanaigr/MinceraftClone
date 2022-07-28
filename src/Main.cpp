#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop

#include <GLFW/glfw3.h>

#include"Units.h"
#include"Position.h"
#include"Vector.h"
#include"Chunk.h"
#include"Viewport.h"
#include"LightingPropagation.h"
#include"Lighting.h"
#include"AO.h"
#include"Area.h"
#include"BlockProperties.h"
#include"BlocksData.h"
#include"Trace.h"
#include"Physics.h"
#include"ChunkGen.h"
#include"NeighbouringEmitters.h"

#include"Config.h"
#include"Font.h"
#include"ShaderLoader.h"
#include"Read.h"
#include"Misc.h"
#include"Counter.h"
#include"SaveBMP.h"

#include<iostream>
#include<chrono>
#include<vector>
#include<sstream>
#include<string>
#include<cstdlib>

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
static vec2i windowSize{ 1920, 1080 };
#else
static vec2i windowSize{ 1280, 720 };
#endif // FULLSCREEN

static vec2i newWindowSize{ windowSize };

static vec2d windowSize_d() { return windowSize.convertedTo<double>(); };
static double aspect() { return windowSize_d().y / windowSize_d().x; };

static bool lockFramerate = false;


static double       deltaTime{ 16.0/1000.0 };
static double const fixedDeltaTime{ 16.0/1000.0 };

GLFWwindow* window;

static int  viewDistance = 3;
static bool loadChunks = false, saveChunks = false;
static std::string worldName{ "world0" };

static vec2d mouseSensitivity{ 0.8, -0.8 };
static int chunkUpdatesPerFrame = 5;

static double const playerHeight{ 1.95 };
static double const playerWidth{ 0.6 };

static int64_t const width_i{ units::posToFracRAway(playerWidth).value() };
static int64_t const height_i{ units::posToFracRAway(playerHeight).value() };
// /*static_*/assert(width_i % 2 == 0);
// /*static_*/assert(height_i % 2 == 0);

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


static bool debugInfo{ false };
static bool numpad[10];

static bool mouseCentered{ true };


enum class BlockAction {
	NONE = 0,
	PLACE,
	BREAK
};
static BlockAction blockAction{ BlockAction::NONE };
namespace LiquidPlaceType { enum LiquidPlaceType { 
	liquid, inflow, outflow,   typesCount
}; }/*
	note: with this method of handling special types, inflow and outflow couldn't
	be placed at the same time
*/
static LiquidPlaceType::LiquidPlaceType liquidPlaceType{ LiquidPlaceType::liquid };
static bool breakFullBlock{ false };
static double const blockActionCD{ 300.0 / 1000.0 };

static const Font font{ "./assets/font.txt" };

enum BitmapTextures : GLuint {
	firstBitmapTexture = 0,
	atlas_it = firstBitmapTexture,
	font_it,
	noise_it,
	
	bitmapTexturesCount
};
enum FramebufferTextures : GLuint {
	firstFramebufferTexture = bitmapTexturesCount,
	screenshotColor_it = firstFramebufferTexture,
	render_it,
	pp1_it,
	pp2_it,
	
	bitmapAndFramebufferTexturesCount
};
static constexpr GLint framebufferTextureCount = bitmapAndFramebufferTexturesCount - firstFramebufferTexture;
static GLuint textures[bitmapAndFramebufferTexturesCount];
static GLuint const		&atlas_t           = textures[atlas_it], 
						&font_t            = textures[font_it], 
						&noise_t           = textures[noise_it],
						&screenshotColor_t = textures[screenshotColor_it],
						&render_t          = textures[render_it],
						&pp1_t             = textures[pp1_it],
						&pp2_t             = textures[pp2_it];

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
	luminance_b,
	
	ssbosCount
};

static constexpr GLuint traceTest_b = 10;
static GLuint traceTest_ssbo;

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
	&atlasDescription_ssbo    = ssbos[atlasDescription_b],
	&luminance_ssbo           = ssbos[luminance_b];
	
static GLuint properties_ub;
static int const ubosCount{ 1 };
static GLuint ubos[ubosCount];
static GLuint &properties_ubo = ubos[properties_ub];

static GLuint mainProgram;
  static GLint rightDir_u;
  static GLint topDir_u;
  static GLint near_u, far_u;
  static GLint playerRelativePosition_u, drawPlayer_u;
  static GLint startChunkIndex_u;
  static GLint startCoord_u;
  static GLint chunksOffset_u;
  static GLint minLogLum_u, rangeLogLum_u;

static GLuint fontProgram;
	
static GLuint currentBlockProgram;
  static GLint cb_blockIndex_u;

static GLuint blockHitbox_p;
  static GLint blockHitboxModelMatrix_u;

static GLuint screenshot_fb;
bool takeScreenshot;

static GLuint render_fb;

static GLuint pp1_fb, pp2_fb;

static GLuint blur_p;
  static GLint sampler_u;

static GLuint toLDR_p;
  static GLint ldr_sampler_u, ldr_blurSampler_u, ldr_exposure_u;

static chunk::Chunks chunks{};


namespace Reload {
	using Flags = uint8_t;
	static constexpr Flags nothing     =       0u;
	static constexpr Flags config      =     0b1u;
	static constexpr Flags texture     =    0b10u;
	static constexpr Flags ssbo        =   0b100u;
	static constexpr Flags framebuffer =  0b1000u;
	static constexpr Flags shader      = 0b10000u;
	static constexpr Flags everything  = 0b11111u;
	
	static bool is(Flags const flags, Flags const value) {
		return (value & flags) == (~nothing & flags);
	}
}

static Reload::Flags reloadConfig();
static void reloadFramebuffers();
static void reloadTextures();
static void reloadShaderStorageBuffers();
static void reloadShaders();

static void reload(Reload::Flags flags) {
	if(Reload::is(Reload::config, flags)) flags |= reloadConfig();
	if(Reload::is(Reload::texture, flags)) reloadTextures();
	if(Reload::is(Reload::ssbo, flags)) reloadShaderStorageBuffers();
	if(Reload::is(Reload::framebuffer, flags)) reloadFramebuffers();
	if(Reload::is(Reload::shader, flags)) reloadShaders();
}


static bool reloadTraceBuffer(bool const is) {
	if(is) {
		const size_t size{ windowSize.x * windowSize.y * (20 * 8*sizeof(uint32_t) + sizeof(uint32_t)) };
		
		glDeleteBuffers(1, &traceTest_ssbo);
		glGenBuffers   (1, &traceTest_ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, traceTest_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, size, NULL, GL_STATIC_COPY);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, traceTest_b, traceTest_ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
		
		if(GLenum errorCode = glGetError(); errorCode == GL_OUT_OF_MEMORY) {
			std::cout << "unable to allocate " << float(double(size) / 1024 / 1024) << " Mb\n";
			return false;
		}
		else {
			std::cout << "trace buffer (" << float(double(size) / 1024 / 1024) << " Mb) enabled\n";
			return true;
		}
	}
	else {
		glDeleteBuffers(1, &traceTest_ssbo);
		std::cout << "trace buffer disabled\n";
		return false;
	}
}
static bool is{ false };	
static void useTraceBuffer() {
	is = reloadTraceBuffer(is);
}


enum class Key : uint8_t { RELEASE = GLFW_RELEASE, PRESS = GLFW_PRESS, REPEAT = GLFW_REPEAT, NOT_PRESSED };
static_assert(GLFW_RELEASE >= 0 && GLFW_RELEASE < 256 && GLFW_PRESS >= 0 && GLFW_PRESS < 256 && GLFW_REPEAT >= 0 && GLFW_REPEAT < 256);

static bool alt{false}, shift{ false }, ctrl{ false };
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
		glfwSetWindowShouldClose(window, GL_TRUE);
	}
	
	if(key == GLFW_KEY_W)
		currentInput().movement.z = 1 * isPress;
	else if(key == GLFW_KEY_S)
		currentInput().movement.z = -1 * isPress;
	else if(key == GLFW_KEY_D)
		currentInput().movement.x = 1 * isPress;
	else if(key == GLFW_KEY_A)
		currentInput().movement.x = -1 * isPress;
	
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
	else if(key == GLFW_KEY_X && !isPress) {
		liquidPlaceType = LiquidPlaceType::LiquidPlaceType( (liquidPlaceType + 1) % LiquidPlaceType::typesCount );
	}
	
	if(key == GLFW_KEY_MINUS && isPress) zoom = 1 + (zoom - 1) * 0.95;
	else if(key == GLFW_KEY_EQUAL/*+*/ && isPress) zoom = 1 + (zoom - 1) * (1/0.95);
	
	if(GLFW_KEY_KP_0 <= key && key <= GLFW_KEY_KP_9  &&  !isPress) numpad[key - GLFW_KEY_KP_0] = !numpad[key - GLFW_KEY_KP_0];
	
	if(action == GLFW_RELEASE) {
		if(ctrl) {
			if(key == GLFW_KEY_F1) {
				reload(reloadConfig());
			}
			else if(key == GLFW_KEY_F2) {
				reloadTextures();
			}
			else if(key == GLFW_KEY_F3) {
				reloadShaderStorageBuffers();
			}
			else if(key == GLFW_KEY_F4) {
				reloadFramebuffers();
			}
			else if(key == GLFW_KEY_F5) {
				reloadShaders();
			}
			else if(key == GLFW_KEY_F6) {
				reload(Reload::everything);
			}
		}
		else {
			if(key == GLFW_KEY_F2)
				debugInfo = !debugInfo;	
			else if(key == GLFW_KEY_F3) { 
				if(!isSpectator) spectatorCoord = currentCameraPos();
				isSpectator = !isSpectator;
			}		
			else if(key == GLFW_KEY_F4)
				takeScreenshot = true;
			else if(key == GLFW_KEY_F5)
				useTraceBuffer();
		}
	}
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) noexcept {
	if(key == GLFW_KEY_UNKNOWN) return;
	if(action == GLFW_REPEAT) return;
	
	if(action == GLFW_PRESS) keys[key] = Key::PRESS;
	else if(action == GLFW_RELEASE) keys[key] = Key::RELEASE;
	
	handleKey(key);
	
	alt = (mods & GLFW_MOD_ALT) != 0;
	shift = (mods & GLFW_MOD_SHIFT) != 0;
    ctrl = (mods & GLFW_MOD_CONTROL) != 0;
}

static vec2<double> pmousePos(0, 0);
static void cursor_position_callback(GLFWwindow* window, double mousex, double mousey) noexcept {
	//https://github.com/glfw/glfw/issues/1523
	
    vec2<double> mousePos{ mousex, mousey };
	if(mouseCentered) {
		deltaRotation += (mousePos - pmousePos) / windowSize_d() * mouseSensitivity;
	}
	pmousePos = mousePos;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) noexcept {
    if(button == GLFW_MOUSE_BUTTON_LEFT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::BREAK : BlockAction::NONE;
	}
	else if(button == GLFW_MOUSE_BUTTON_RIGHT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::PLACE : BlockAction::NONE;
	}
	else if(button == GLFW_MOUSE_BUTTON_MIDDLE) {
		isSmoothCamera = action != GLFW_RELEASE;
		if(!isSmoothCamera) {
			viewportDesired.rotation = viewportCurrent.rotation;
		}
	}
}

static int blockPlaceId = 1;
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) noexcept {
	blockPlaceId = 1+misc::mod<int>(blockPlaceId-1 + int(yoffset), Blocks::blocksCount-1);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept {
	newWindowSize = vec2i{ width, height }.max(1);
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
	static chunk::PackedAABB<pBlock> aabb{ pBlock{units::blocksInChunkDim-1}, pBlock{0} };
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

static Reload::Flags reloadConfig() {
	Reload::Flags flags{};
	
	Config cfg{};
		cfg.viewDistance =  viewDistance;
		cfg.loadChunks = saveChunks;
		cfg.saveChunks = saveChunks;
		cfg.playerCameraFOV = desiredPlayerCamera.fov;
		cfg.mouseSensitivity = mouseSensitivity;
		cfg.chunkUpdatesPerFrame = chunkUpdatesPerFrame;
		cfg.lockFramerate = lockFramerate;
		cfg.worldName = worldName;
		
	parseConfigFromFile(cfg);
	
	if(viewDistance != cfg.viewDistance) flags |= Reload::ssbo;
	
	viewDistance = cfg.viewDistance;
	loadChunks = cfg.loadChunks;
	saveChunks = cfg.saveChunks;
	playerCamera.fov = desiredPlayerCamera.fov = cfg.playerCameraFOV;
	mouseSensitivity = cfg.mouseSensitivity;
	chunkUpdatesPerFrame = cfg.chunkUpdatesPerFrame;
	lockFramerate = cfg.lockFramerate;
	worldName = cfg.worldName;
	
	return flags;
}

static void reloadFramebuffers() {
	glDeleteTextures(framebufferTextureCount, &textures[firstFramebufferTexture]);
	glGenTextures   (framebufferTextureCount, &textures[firstFramebufferTexture]);
	
	{ //redner framebuffer
		glActiveTexture(GL_TEXTURE0 + pp1_it);
		glBindTexture(GL_TEXTURE_2D, pp1_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, windowSize.x, windowSize.y, 0, GL_RGBA, GL_FLOAT, NULL);
		
		glActiveTexture(GL_TEXTURE0 + pp2_it);
		glBindTexture(GL_TEXTURE_2D, pp2_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, windowSize.x, windowSize.y, 0, GL_RGBA, GL_FLOAT, NULL);
		
		glActiveTexture(GL_TEXTURE0 + render_it);
		glBindTexture(GL_TEXTURE_2D, render_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, windowSize.x, windowSize.y, 0, GL_RGB, GL_FLOAT, NULL);	
		
		
		glDeleteFramebuffers(1, &render_fb);
		glGenFramebuffers(1, &render_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, render_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_t, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pp1_t   , 0);
		if (GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "render framebuffer: error %u", status);
		}
			
		glDeleteFramebuffers(1, &pp1_fb);
		glGenFramebuffers(1, &pp1_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, pp1_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pp1_t, 0);
		if(GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "pp1_fb: error %u\n", status);
		}
		
		glDeleteFramebuffers(1, &pp2_fb);
		glGenFramebuffers(1, &pp2_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, pp2_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pp2_t, 0);
		if(GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "pp2_fb: error %u\n", status);
		}
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}	
	
	{ //screenshot framebuffer
		glActiveTexture(GL_TEXTURE0 + screenshotColor_it);
		glBindTexture(GL_TEXTURE_2D, screenshotColor_t);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, windowSize.x, windowSize.y, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		  
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
}

static void reloadTextures() {
	glDeleteTextures(bitmapTexturesCount, &textures[firstBitmapTexture]);
	glGenTextures   (bitmapTexturesCount, &textures[firstBitmapTexture]);
	
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
	
	{ //atlas texture
		Image image;
		ImageLoad("assets/atlas.bmp", &image);
	
		glActiveTexture(GL_TEXTURE0 + atlas_it);
		glBindTexture(GL_TEXTURE_2D, atlas_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.get());
	}
	
	{ //font texture
		Image image;
		ImageLoad("assets/font.bmp", &image);
		
		glActiveTexture(GL_TEXTURE0 + font_it);
		glBindTexture(GL_TEXTURE_2D, font_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.get());
	}
}

static void reloadShaderStorageBuffers() {
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
			c(18, 0), c(18, 0), c(18, 0), c(0, 1), //water
			c(19, 0), c(19, 0), c(19, 0), c(19, 1), //grass
			c(20, 0), c(20, 0), c(20, 0), c(20, 1), //glassMagentaBlock
			c(21, 0), c(21, 0), c(21, 0), c(21, 1), //glassVioletBlock
			c(22, 0), c(22, 0), c(22, 0), c(22, 1), //glassBlueBlock
			c(23, 0), c(23, 0), c(23, 0), c(23, 1), //glassCyanBlock
			c(24, 0), c(24, 0), c(24, 0), c(24, 1), //glassTurquoiseBlock
			c(25, 0), c(25, 0), c(25, 0), c(25, 1), //glassGreenBlock
			c(26, 0), c(26, 0), c(26, 0), c(26, 1), //glassYellowBlock
			c(27, 0), c(27, 0), c(27, 0), c(27, 1), //glassOrangeBlock
			c(28, 0), c(28, 0), c(28, 0), c(28, 1), //glassRedBlock
		};
		
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, atlasDescription_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(sides), &sides, GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, atlasDescription_b, atlasDescription_ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	
	{ //luminance histogram
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, luminance_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 256 * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, luminance_b, luminance_ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);	
	}
	
	{ //reload the only UBO as well
		glDeleteBuffers(ubosCount, &ubos[0]);
		glGenBuffers   (ubosCount, &ubos[0]);
		
		glBindBuffer(GL_UNIFORM_BUFFER, properties_ubo);
		glBufferData(GL_UNIFORM_BUFFER, 8 + 4 + 4 + 4*4*4, NULL, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, properties_ub, properties_ubo); 
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}
	
	gpuBuffersReseted();
}

static void reloadShaders() {
	char const *const triangleVertex{ R"(
		#version 460				

		void main() {
			const vec2 verts[] = {
				vec2(-1, -1),
				vec2(+3, -1),
				vec2(-1, +3)
			};
			gl_Position = vec4(verts[gl_VertexID], 0, 1);
		}
	)" };
	
	auto const printLinkErrors = [](GLuint const prog, char const *const name) {
		GLint progErrorStatus;
		glGetProgramiv(prog, GL_LINK_STATUS, &progErrorStatus);
		if(progErrorStatus == 1) return;
		
		std::cout << "Program status: " << progErrorStatus << '\n';
		int length;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
		
		static int maxLenght = 0;
		static GLchar *msg = nullptr;
		if(maxLenght < length) {
			maxLenght = length;
			delete[] msg;
			msg = new GLchar[maxLenght];
		}

		glGetProgramInfoLog(prog, length, &length, msg);
		std::cout << "Program \"" << name << "\" error:\n" << msg;
	};
	
	auto const bindProperties = [](GLuint const prog) {
		auto const index{ glGetUniformBlockIndex(prog, "Properties") };
		glUniformBlockBinding(prog, index, properties_ub);
	};
	
	{ //main program
		glDeleteProgram(mainProgram);
		mainProgram = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromCode(triangleVertex, GL_VERTEX_SHADER  , "main vertex");
		sl.addShaderFromProjectFileName("shaders/main.frag", GL_FRAGMENT_SHADER, "main shader");
	
		sl.attachShaders(mainProgram);
	
		glLinkProgram(mainProgram);
		printLinkErrors(mainProgram, "main");
		glValidateProgram(mainProgram);
	
		sl.deleteShaders();
	
		glUseProgram(mainProgram);		
		bindProperties(mainProgram);
		
		glUniform1f(glGetUniformLocation(mainProgram, "playerWidth" ), playerWidth );
		glUniform1f(glGetUniformLocation(mainProgram, "playerHeight"), playerHeight);

		rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
		topDir_u = glGetUniformLocation(mainProgram, "topDir");
		near_u = glGetUniformLocation(mainProgram, "near");
		far_u = glGetUniformLocation(mainProgram, "far");
	
		startCoord_u = glGetUniformLocation(mainProgram, "startCoord");
		chunksOffset_u = glGetUniformLocation(mainProgram, "chunksOffset");
		
		minLogLum_u = glGetUniformLocation(mainProgram, "minLogLum");
		rangeLogLum_u = glGetUniformLocation(mainProgram, "rangeLogLum");
		
		playerRelativePosition_u = glGetUniformLocation(mainProgram, "playerRelativePosition");
		drawPlayer_u = glGetUniformLocation(mainProgram, "drawPlayer");
		
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
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "TraceTest"), traceTest_b);
		glShaderStorageBlockBinding(mainProgram, glGetProgramResourceIndex(mainProgram, GL_SHADER_STORAGE_BLOCK, "Luminance"), luminance_b);
		ce
	}
	
	{ //font program
		glDeleteProgram(fontProgram);
		fontProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromProjectFileName("shaders/font.vert", GL_VERTEX_SHADER,"font vertex");
		sl.addShaderFromProjectFileName("shaders/font.frag", GL_FRAGMENT_SHADER, "font fragment");
		
		sl.attachShaders(fontProgram);
	
		glLinkProgram(fontProgram);
		printLinkErrors(fontProgram, "font");
		glValidateProgram(fontProgram);
	
		sl.deleteShaders();
	
		glUseProgram(fontProgram);
		
		GLuint const fontTex_u = glGetUniformLocation(fontProgram, "font");
		glUniform1i(fontTex_u, font_it);
		
		bindProperties(fontProgram);
		ce
	}
	
	{ //block hitbox program
		glDeleteProgram(blockHitbox_p);
		blockHitbox_p = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromProjectFileName("shaders/blockHitbox.vert", GL_VERTEX_SHADER  , "Block hitbox vertex");
		sl.addShaderFromProjectFileName("shaders/blockHitbox.frag", GL_FRAGMENT_SHADER, "Block hitbox fragment");
	
		sl.attachShaders(blockHitbox_p);
	
		glLinkProgram(blockHitbox_p);
		printLinkErrors(blockHitbox_p, "block hitbox");
		glValidateProgram(blockHitbox_p);
	
		sl.deleteShaders();
	
		glUseProgram(blockHitbox_p);
		
		glUniform1i(glGetUniformLocation(blockHitbox_p, "atlas"), atlas_it);
		glUniform1f(glGetUniformLocation(blockHitbox_p, "atlasTileSize"),  16); //from main prgram
		
		blockHitboxModelMatrix_u = glGetUniformLocation(blockHitbox_p, "modelMatrix");
		bindProperties(blockHitbox_p);
		ce
	}
	
	{ //current block program
		glDeleteProgram(currentBlockProgram);
		currentBlockProgram = glCreateProgram();
		ShaderLoader sl{};
		
		sl.addShaderFromProjectFileName("shaders/curBlock.vert", GL_VERTEX_SHADER,"cur block vertex");
		sl.addShaderFromProjectFileName("shaders/curBlock.frag", GL_FRAGMENT_SHADER, "current block fragment");
		
		sl.attachShaders(currentBlockProgram);
	
		glLinkProgram(currentBlockProgram);
		printLinkErrors(currentBlockProgram, "current block");
		glValidateProgram(currentBlockProgram);
	
		sl.deleteShaders();
	
		glUseProgram(currentBlockProgram);
		
		glUniform1i(glGetUniformLocation(currentBlockProgram, "atlas"), atlas_it);
		cb_blockIndex_u = glGetUniformLocation(currentBlockProgram, "block");
		glUniform1f(glGetUniformLocation(currentBlockProgram, "atlasTileSize"), 16); //from main program
		bindProperties(currentBlockProgram);
		
		glShaderStorageBlockBinding(currentBlockProgram, glGetProgramResourceIndex(currentBlockProgram, GL_SHADER_STORAGE_BLOCK, "AtlasDescription"), atlasDescription_b);
		ce
	}

	{ //blur program
		glDeleteProgram(blur_p);
		blur_p = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromCode(triangleVertex, GL_VERTEX_SHADER, "blur vertex");
		sl.addShaderFromProjectFileName("shaders/blur.frag", GL_FRAGMENT_SHADER, "blur shader");
	
		sl.attachShaders(blur_p);
	
		glLinkProgram(blur_p);
		printLinkErrors(blur_p, "blur");
		glValidateProgram(blur_p);
	
		sl.deleteShaders();
		
		glUseProgram(blur_p);
		
		sampler_u = glGetUniformLocation(blur_p, "sampler");
		ce
	}
	
	{ //toLDR program
		glDeleteProgram(toLDR_p);
		toLDR_p = glCreateProgram();
		ShaderLoader sl{};

		sl.addShaderFromCode(triangleVertex, GL_VERTEX_SHADER, "to LDR vertex");
		sl.addShaderFromProjectFileName("shaders/toLDR.frag", GL_FRAGMENT_SHADER, "to LDR shader");
	
		sl.attachShaders(toLDR_p);
	
		glLinkProgram(toLDR_p);
		printLinkErrors(toLDR_p, "to LDR");
		glValidateProgram(toLDR_p);
	
		sl.deleteShaders();
		
		glUseProgram(toLDR_p);
		
		ldr_sampler_u = glGetUniformLocation(toLDR_p, "sampler");
		ldr_blurSampler_u = glGetUniformLocation(toLDR_p, "blurSampler");
		ldr_exposure_u = glGetUniformLocation(toLDR_p, "exposure");
		ce
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


static bool updateChunk(chunk::Chunk chunk, vec3i const cameraChunkCoord, bool const maxUpdated) {
	static constexpr bool updateChunkDebug = false;
	
	auto &status{ chunk.status() };
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
			
			chunk::PackedAABB<pBlock> const aabbData{ chunk.aabb() };
			
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

static void updateChunks(chunk::Chunks &chunks) {
	if(numpad[1]) return;
	
	static std::vector<bool> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.clear();
	chunksPresent.resize(viewWidth*viewWidth);
	size_t notPresentCount{};
	
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
			else {
				notPresentCount++;
				return false;
			}
		}, 
		[&](int const chunkIndex) -> void { //free the chunk
			auto chunk{ chunks[chunkIndex] };
			gpuChunksIndex.recycle(chunk.gpuIndex());
			chunks.chunksIndex_position.erase( chunk.position() );
			if(chunk.modified() && saveChunks) writeChunk(chunk, worldName); /*
				note: it is not 100% safe to write chunk whose column neighbours may be deleted.
				WriteChunk goes through all of the chunks in the column without using neighbours info,
				so it works for now.
			*/
			
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

	for(int k = 0; k < viewWidth; k ++) 
	for(int i = 0; i < viewWidth; i ++) {
		auto const index{ chunksPresent[i+k*viewWidth] };
		if(index == false) { //generate chunk
			auto const relativeChunksPos{ vec2i{i, k} - vec2i{viewDistance} };
			auto const chunksPos{ currentChunk.xz() + relativeChunksPos };
			
			genChunksColumnAt(chunks, chunksPos, worldName, loadChunks);
		}		
	}
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
			
			chunks.liquidCubes.add({neighbourChunkIndex.get(), chunk::cubeCoordToIndex(neighbourInChunkCoord)});
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
		
		auto const [chunk, cubeInChunkPos] = [&]() {
			auto const startBlockId{ startChunk.blocks()[result.cubeInChunkCoord.as<pBlock>()].id() };
			auto const canPlaceStartingBlock{ placeThrough(startBlockId) };
			
			if(canPlaceStartingBlock && !ctrl) {
				return std::make_tuple(startChunk, result.cubeInChunkCoord);
			}
			else {
				auto const cubePosInStartChunk { result.cubeInChunkCoord + pCube{normal} };
			
				auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{startChunk}.moveToNeighbour(cubePosInStartChunk.valAs<pChunk>()) };
				if(!chunkIndex.is()) return std::make_tuple(chunk::Chunk(chunks, -1), pCube{0});
				auto chunk{ chunks[chunkIndex.get()] };
				
				return std::make_tuple(chunk, cubePosInStartChunk.in<pChunk>());
			}
		}();
		if(chunk.chunkIndex() == -1) return false;
		
		auto &liquid{ chunk.liquid() };
		pChunk const chunkPos{ chunk.position() };
		auto const blockInChunkPos{ cubeInChunkPos.as<pBlock>() };
		auto const blockInChunkCoord{ blockInChunkPos.val() };
		
		pCube const first{ blockInChunkPos                        };
		pCube const last { blockInChunkPos + pBlock{1} - pCube{1} };
		
		if(blockPlaceId == Blocks::water) {
			auto const cubeId{ chunk.data().cubeAt2(cubeInChunkPos) };
				
			if(placeThrough(cubeId)) {
				if(liquidPlaceType == LiquidPlaceType::liquid) {
					liquid[cubeInChunkPos] = chunk::LiquidCube::liquid(blockPlaceId, chunk::LiquidCube::maxLevel, false);
				}
				else if(liquidPlaceType == LiquidPlaceType::inflow) {
					liquid[cubeInChunkPos] = chunk::LiquidCube::special(blockPlaceId, 5, true, false);
				}
				else if(liquidPlaceType == LiquidPlaceType::outflow) {
					liquid[cubeInChunkPos] = chunk::LiquidCube::special(blockPlaceId, chunk::LiquidCube::minLevel/*!*/, false, true);			
				}
				else assert(false && "unreachable (LiquidPlaceType)");
				
				chunks.liquidCubes.add({chunk.chunkIndex(), chunk::cubeCoordToIndex(cubeInChunkPos)});
				
				chunk.modified() = true;
				auto &aabb{ chunk.aabb() };
				aabb = aabb + Area{blockInChunkCoord, blockInChunkCoord};
				
				updateBlocksDataInArea(chunk, first.as<pBlock>() - pBlock{1}, last.as<pBlock>() + pBlock{1});
				
				iterate3by3Volume([&, chunk = chunk](vec3i const dir, int const index) {
					auto const chunkOffset{ (blockInChunkPos + pBlock{dir}).valAs<pChunk>() };
					auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(chunkOffset) };
					if(!chunkIndex.is()) return;
					auto &chunkStatus{ chunks[chunkIndex.get()].status() };
					
					chunkStatus.setBlocksUpdated(true);
				});
			}
			else return false;
		}
		else {
			auto &block{ chunk.data()[blockInChunkCoord] };
				
			if(checkCanPlaceBlock(chunkPos + blockInChunkPos) && placeThrough(block.id())) {
				block = chunk::Block::fullBlock(blockPlaceId);
				
				if(!liquidThrough(blockPlaceId)) {
					for(int i{}; i < pos::cubesInBlockCount; i++) {
						auto const curCubeCoord{ blockInChunkPos + pCube{chunk::Block::cubeIndexPos(i)} };	
						liquid[curCubeCoord] = chunk::LiquidCube{};
					}
				}
				
				auto const emitter{ isBlockEmitter(blockPlaceId) };
				
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
				
				iterate3by3Volume([&, chunk = chunk](vec3i const dir, int const index) {
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
	
	auto newCamera{ desiredPlayerCamera };
	newCamera.fov = misc::lerp( playerCamera.fov, desiredPlayerCamera.fov / curZoom, 0.1 );
	playerCamera = newCamera;
	
	updateChunks(chunks);
	if(!numpad[0]) chunks.liquidCubes.update();
	
	auto const diff{ pos::fracToPos(playerCoord+playerCameraOffset - playerCamPos) };
	playerCamPos = pFrac{ 
		(playerCamPos + pos::posToFrac(vec3lerp(vec3d{}, vec3d(diff), vec3d(0.4)))).val()
			.clamp((playerCoord+playerCameraOffset).val() - width_i/3, (playerCoord+playerCameraOffset).val() + width_i/3)
			//'width / 3' is arbitrary value less than width/2
	};
	
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

int main() {
	auto const startupTime{ std::chrono::steady_clock::now() };
    if (!glfwInit()) return -1;

    GLFWmonitor* monitor;
#ifdef FULLSCREEN
    monitor = glfwGetPrimaryMonitor();
#else
    monitor = NULL;
#endif // !FULLSCREEN
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
    window = glfwCreateWindow(windowSize.x, windowSize.y, "Minceraft clone", monitor, NULL);
	
    if (!window) {
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
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	
	glfwGetCursorPos(window, &pmousePos.x, &pmousePos.y);
    glfwSetScrollCallback(window, scroll_callback);
	
	
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
	
	
	while ((err = glGetError()) != GL_NO_ERROR) {
        std::cout << err << std::endl;
    }
	
	//useTraceBuffer();

	auto const a1{ std::chrono::steady_clock::now() };
	reloadConfig();
	auto const a2{ std::chrono::steady_clock::now() };
	updateChunks(chunks);
	auto const a3{ std::chrono::steady_clock::now() };
	reload(Reload::everything & ~Reload::config);
	auto const a4{ std::chrono::steady_clock::now() };
	
	#define b(ARG) ( double(std::chrono::duration_cast<std::chrono::microseconds>(ARG).count()) / 1000.0 )
	std::cout << "reload config: " << b(a2 - a1) << ", update chunks: " << b(a3 - a2) << ", reload shaders " << b(a4 - a3) << '\n';
	#undef b
	
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
		
		auto const cameraCoord{ currentCameraPos() };
		auto const cameraChunk{ cameraCoord.valAs<pos::Chunk>() };
		auto const cameraPosInChunk{ pos::fracToPos(cameraCoord.in<pos::Chunk>()) };
		
		if(newWindowSize != windowSize) {
			auto const resizeFB{ (newWindowSize > windowSize).any() };
			windowSize = newWindowSize;
			
			if(resizeFB) {
				reloadFramebuffers();
				reloadTraceBuffer(is);
			}
			
			glViewport(0, 0, windowSize.x, windowSize.y);
		}
		
		{ //set properties
			//screen size
			GLint screenSize[2] = { windowSize.x, windowSize.y };
			
			//projection
			float projection[4][4];
			currentCamera().projectionMatrix(aspect(), &projection);
		
			GLfloat projectionT[4][4];
			
			for(int i{}; i < 4; i++)
			for(int j{}; j < 4; j++) 
				projectionT[i][j] = projection[j][i];
			
			//time
			static double lastTime{};
			double curTime{ std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - completionTime).count() / 1000.0 };
			
			static double offset{};
			if(!numpad[2]) offset += curTime - lastTime;
			lastTime = curTime;
			GLfloat const time( offset / 1000.0 );
			
			//
			glBindBuffer(GL_UNIFORM_BUFFER, properties_ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, 8, &screenSize);
			glBufferSubData(GL_UNIFORM_BUFFER, 8, 4, &time);
			glBufferSubData(GL_UNIFORM_BUFFER, 16, 4 * 4*4, &projectionT[0][0]);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
		
		
		{ //draw world
			//glClear(GL_COLOR_BUFFER_BIT);
			glDisable(GL_DEPTH_TEST); 
			glDisable(GL_CULL_FACE); 
			
			glUseProgram(mainProgram);
			
			glUniform3f(rightDir_u, rightDir.x, rightDir.y, rightDir.z);
			glUniform3f(topDir_u, topDir.x, topDir.y, topDir.z);
			
			auto const &currentCam{ currentCamera() };
			glUniform1f(near_u, currentCam.near);
			glUniform1f(far_u , currentCam.far );
			
			static pChunk lastCameraChunkCoord{};
			
			if(!numpad[1]) lastCameraChunkCoord = pChunk{cameraChunk};
			auto const startCoord{ pos::fracToPos(cameraCoord - lastCameraChunkCoord + pChunk{viewDistance}) };
			auto const chunksOffset{ lastCameraChunkCoord - pChunk{viewDistance} };
			
			glUniform3f(startCoord_u, startCoord.x, startCoord.y, startCoord.z);
			glUniform3i(chunksOffset_u, chunksOffset->x, chunksOffset->y, chunksOffset->z);
		
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
			
			//clear luminance histogram
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, luminance_ssbo);
			glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R8, 0, 256 * sizeof(uint32_t), GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
			
			
			if(numpad[3]) glFinish();
			auto const startTraceTime{ std::chrono::steady_clock::now() };
			
			auto const minExp{ 0.1f };
			auto const maxExp{ 5.0f };
			auto const minLogLum{ std::log2(minExp) };
			auto const maxLogLum{ std::log2(maxExp) };
			auto const rangeLogLum{ maxLogLum - minLogLum };
			const float x = (255.0 / rangeLogLum);
			
			glUniform1f(minLogLum_u, minLogLum);
			glUniform1f(rangeLogLum_u, rangeLogLum);
			
			glBindFramebuffer(GL_FRAMEBUFFER, render_fb);
			GLenum buffers1[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
			glDrawBuffers(2, buffers1);		
			
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
			
			GLenum buffers2[] = { GL_COLOR_ATTACHMENT0 };
			glDrawBuffers(1, buffers2);
			
			
			uint32_t luminanceHistogram[256];
			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 256 * sizeof(uint32_t), (void*) &luminanceHistogram);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			//https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
			/*auto const curLum{ [&]() { //median
				auto const total{ windowSize.x * windowSize.y };
				
				auto curCount{ 0 };
				for(int n{0}; n < 256; n++) {
					auto const count{ luminanceHistogram[n] };
					curCount += count;
					
					auto const diff{ curCount - (total*3) / 4 };
					if(diff >= 0) return misc::lerp( powf(2, (n)/x), powf(2, (n - 1)/x), float(diff) / count) * minExp;
				}
				
				assert(false && "unreachable");
				return 1.0f;
			}() };*/
			auto const curLum{ [&]() { //geometric mean
				auto const total{ windowSize.x * windowSize.y };
				
				float result{ 0.0 };
				for(int n{0}; n < 256; n++) {
					auto const count{ luminanceHistogram[n] };
					
					result += count * (float(n)/x + minLogLum) * log(2);
				}
				
				return expf(result / total);
			}() };
			/*auto const curLum{ [&]() { //arithmetic mean
				auto const total{ windowSize.x * windowSize.y };
				
				double result{ 0.0 };
				for(int n{0}; n < 256; n++) {
					auto const count{ luminanceHistogram[n] };
					
					result += count * pow(2.0, double(n)/x);
				}
				
				return float(result / total * minExp);
			}() };*/
			
			static float prevLum{ curLum };
			prevLum = prevLum + (curLum - prevLum) * (1 - exp(-deltaTime * 1.25));
			auto const exposure{ 0.23 / (misc::clamp(prevLum, 0.1f, 5.0f) + 0.2) };
			
			//std::cout << curLum <<  ' ' << prevLum << '\n';
			
			if(!numpad[5]) {
				glUseProgram(blur_p);
				GLint const h_u{ glGetUniformLocation(blur_p, "h") };
				
				glBindFramebuffer(GL_FRAMEBUFFER, pp2_fb);
				glUniform1i(sampler_u, pp1_it);
				glUniform1ui(h_u, 0);
				glDrawArrays(GL_TRIANGLES, 0, 3);	
				
				glBindFramebuffer(GL_FRAMEBUFFER, pp1_fb);
				glUniform1i(sampler_u, pp2_it);
				glUniform1ui(h_u, 1);
				glDrawArrays(GL_TRIANGLES, 0, 3);	
				
				glBindFramebuffer(GL_FRAMEBUFFER, pp2_fb);
				glUniform1i(sampler_u, pp1_it);
				glUniform1ui(h_u, 0);
				glDrawArrays(GL_TRIANGLES, 0, 3);	
				
				glBindFramebuffer(GL_FRAMEBUFFER, pp1_fb);
				glUniform1i(sampler_u, pp2_it);
				glUniform1ui(h_u, 1);
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			else {
				glBindFramebuffer(GL_FRAMEBUFFER, pp1_fb);
				glClear(GL_COLOR_BUFFER_BIT);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
			
			glUseProgram(toLDR_p);
			
			if(takeScreenshot) glBindFramebuffer(GL_FRAMEBUFFER, screenshot_fb);
			else               glBindFramebuffer(GL_FRAMEBUFFER, 0);
			
			glUniform1f(ldr_exposure_u, exposure);
			glUniform1i(ldr_sampler_u, render_it);
			glUniform1i(ldr_blurSampler_u, pp1_it);
			
			glDrawArrays(GL_TRIANGLES, 0, 3);
			
			if(numpad[3]) glFinish();
			auto const endTraceTime{ std::chrono::steady_clock::now() };
			
			if(takeScreenshot) {
				takeScreenshot = false;
				glFinish();
				
				static uint8_t *data{ 0 };
				static int size{ 0 };
				if(size < 3 * windowSize.x * windowSize.y) {
					delete[] data;
					data = new uint8_t[3 * windowSize.x * windowSize.y];
				}
				
				glActiveTexture(GL_TEXTURE0 + screenshotColor_it);
				glGetTexImage(GL_TEXTURE_2D, 0, GL_BGR, GL_UNSIGNED_BYTE, data);
				
				generateBitmapImage(data, windowSize.y, windowSize.x, "screenshot.bmp");
				
				glBindFramebuffer(GL_READ_FRAMEBUFFER, screenshot_fb);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
				glBlitFramebuffer(	
					0, 0, windowSize.x, windowSize.y,
					0, 0, windowSize.x, windowSize.y,
					GL_COLOR_BUFFER_BIT, GL_NEAREST 
				);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
			
			auto const diffMs{ std::chrono::duration_cast<std::chrono::microseconds>(endTraceTime - startTraceTime).count() / 1000.0 };
			timeToTrace.add(diffMs);
		}
		
		{ //draw block hitbox
			PosDir const pd{ PosDir(cameraCoord, pos::posToFracTrunk(forwardDir * 7).value()) };
			auto const optionalResult{ trace(chunks, pd) };
				
			if(optionalResult) {
				auto const result{ *optionalResult };
				auto const chunk { result.chunk };
				
				auto const blockRelativePos{ vec3f(pos::fracToPos(
					(breakFullBlock ? result.cubePos.as<pBlock>().as<pCube>() : result.cubePos) - cameraCoord
				)) };
				float const size{ breakFullBlock ? 1.0f : (1.0f / units::cubesInBlockDim) };
				
				float toLoc[3][3];
				float toGlob[3][3];
				viewportCurrent.localToGlobalSpace(&toGlob);
				viewportCurrent.globalToLocalSpace(&toLoc);
				
				float const toLoc4[4][4] = {
					{ toLoc[0][0], toLoc[0][1], toLoc[0][2], 0 },
					{ toLoc[1][0], toLoc[1][1], toLoc[1][2], 0 },
					{ toLoc[2][0], toLoc[2][1], toLoc[2][2], 0 },
					{ 0          , 0          , 0          , 1 },
				};

				glEnable(GL_FRAMEBUFFER_SRGB);
				drawBlockHitbox(blockRelativePos, size, toLoc4);
				glDisable(GL_FRAMEBUFFER_SRGB); 
			}
		}
		
		{ //draw current block
			glUseProgram(currentBlockProgram);
			glUniform1ui(cb_blockIndex_u, blockPlaceId);
			glEnable(GL_FRAMEBUFFER_SRGB); 
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			glDisable(GL_FRAMEBUFFER_SRGB); 
		}
		
		{ //draw text
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
							misc::printBinary(ss, blockData.solidCubes); ss << ' ';
							ss << "liquid cubes=";
							misc::printBinary(ss, blockData.liquidCubes); ss << '\n';
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
			
			vec2f const startPoint(textPos.x / windowSize_d().x * 2 - 1, 1 - textPos.y / windowSize_d().y * 2);
			
			auto const lineH = font.base();
			auto const lineHeight = font.lineHeight();
			float const scale = 5;
			auto const aspectRatio{ aspect() };

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
					currentPoint + vec2f(fc.width*aspectRatio, 0 + lineH) / scale + charOffset, 
					//uv
					vec2f{fc.x, 1-fc.y-fc.height},
					vec2f{fc.x+fc.width,1-fc.y},
				};
				
				currentPoint += vec2f(fc.xAdvance*aspectRatio, 0) / scale;
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
		
		
		glfwSwapBuffers(window);
		glfwPollEvents();
		
		ce
		
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
	
	if(saveChunks) {
		int count = 0;
		
		for(auto const chunkIndex : chunks.usedChunks()) {
			auto chunk{ chunks[chunkIndex] };
			if(chunk.modified()) {
				writeChunk(chunk, worldName);
				count++;
			}
		}
		
		std::cout << "saved chunks count: " << count;
	}
	
    return 0;
}