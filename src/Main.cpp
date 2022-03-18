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

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2<uint32_t> windowSize{ 1920, 1080 };
#else
static const vec2<uint32_t> windowSize{ 1280, 720 };
#endif // FULLSCREEN

static const vec2<double> windowSize_d{ windowSize.convertedTo<double>() };

static vec2<double> mousePos(0, 0), pmousePos(0, 0);

static bool isPan{ false };
static bool isZoomMovement{ false };
static double zoomMovement{ 0 };
static double size = 1;

static bool shift{ false }, ctrl{ false };

static vec3<double> movementDir{};
static double speedModifier = 2.5;

static bool debug{ false };

static double deltaTime{ 16.0/1000.0 };
static double const fixedDeltaTime{ 16.0/1000.0 };

static double movementSpeed{ 8 };
static bool jump{ false };

static double const aspect{ windowSize_d.y / windowSize_d.x };

static bool isOnGround{false};
static vec3d playerForce{};
static ChunkCoord playerCoord_{ vec3i{0,0,0}, vec3d{0.01,12.001,0.01} };
static ChunkCoord spectatorCoord{ playerCoord_ };
static Viewport playerViewport{ 
	vec2d{ misc::pi / 2.0, 0 },
	aspect,
	90.0 / 180.0 * misc::pi,
	0.001,
	400
};

static double const height{ 1.95 };
static double const width{ 0.6 };

static int64_t const width_i{ ChunkCoord::posToFracRAway(width).x }; 
static int64_t const height_i{ ChunkCoord::posToFracRAway(height).x };
	
static  vec3d const viewportOffset_{0,height*0.9,0};
static bool isFreeCam{ false };

static Viewport &viewport_current() {
    return playerViewport;
}
static ChunkCoord &playerCoord() {
	if(isFreeCam) return spectatorCoord;
	else return playerCoord_;
}

static vec3d viewportOffset() {
	return viewportOffset_ * (!isFreeCam);
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


static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    bool isPress = !(action == GLFW_RELEASE);
	if(key == GLFW_KEY_ESCAPE && !isPress) {
		mouseCentered = !mouseCentered;
		if(mouseCentered) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	if(key == GLFW_KEY_W)
		movementDir.z = 1 * isPress;
	else if(key == GLFW_KEY_S)
		movementDir.z = -1 * isPress;
	
	if(isFreeCam) {
		if (key == GLFW_KEY_Q)
			movementDir.y = 1 * isPress;
		else if(key == GLFW_KEY_E)
			movementDir.y = -1 * isPress;
	}
	else if (key == GLFW_KEY_SPACE) {
		jump = isPress;
	}
		
	if (key == GLFW_KEY_KP_0 && !isPress) 
			debugBtn0 = !debugBtn0;
	if (key == GLFW_KEY_KP_1 && !isPress) 
		debugBtn1 = !debugBtn1;	

	if(key == GLFW_KEY_D)
		movementDir.x = 1 * isPress;
	else if(key == GLFW_KEY_A)
		movementDir.x = -1 * isPress;
	
	if(key == GLFW_KEY_F5 && action == GLFW_PRESS)
		reloadShaders();
	else if(key == GLFW_KEY_F4 && action == GLFW_PRESS)
		debug = !debug;
	else if(key == GLFW_KEY_F3 && action == GLFW_PRESS) { 
		isFreeCam = !isFreeCam;
		if(isFreeCam) {
			spectatorCoord = playerCoord_ + viewportOffset_;
		}
	}
	else if(key == GLFW_KEY_TAB && action == GLFW_PRESS) testInfo = true;

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
    mousePos = vec2<double>(relativeTo.x + mousePos_.x, -relativeTo.y + windowSize_d.y - mousePos_.y);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if(button == GLFW_MOUSE_BUTTON_LEFT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::BREAK : BlockAction::NONE;
		else isPan = action != GLFW_RELEASE;
	}
	else if(button == GLFW_MOUSE_BUTTON_RIGHT) {
		if(mouseCentered) blockAction = action == GLFW_PRESS ? BlockAction::PLACE : BlockAction::NONE;
	}
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            zoomMovement = 0;
            isZoomMovement = true;
        }
        else if (action == GLFW_RELEASE) {
            isZoomMovement = false;
            zoomMovement = 0;
        }
	}
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    size -= size * yoffset * 0.07;
	blockId = 1+misc::mod(blockId-1 + int(yoffset), 5);
}

static const Font font{ ".\\assets\\font.txt" };

static size_t const texturesCount = 2;
static GLuint textures[texturesCount];
static GLuint &atlas_t = textures[0], &font_t = textures[1];

static GLuint mainProgram = 0;
  static GLuint rightDir_u;
  static GLuint topDir_u;
  static GLuint near_u, far_u;
  static GLuint isInChunk_u;
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

static GLuint bgProjection_u;
static GLuint bgRightDir_u;
static GLuint bgTopDir_u;


static GLuint bgProgram = 0;
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
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * 27 * sizeof(int32_t), NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, chunksNeighbours_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void reloadShaders() {
	glDeleteTextures(texturesCount, &textures[0]);
	glGenTextures(texturesCount, &textures[0]);
	
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
	
		isInChunk_u = glGetUniformLocation(mainProgram, "isInChunk");
		
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
	
		Image image;
		ImageLoad("assets/atlas.bmp", &image);
	
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, atlas_t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data);
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

		//resizeBuffer(0);
	}
		

	{
		glDeleteProgram(bgProgram);
		bgProgram = glCreateProgram();
		ShaderLoader bgsl{};
		bgsl.addScreenSizeTriangleStripVertexShader("bg vertex");
		bgsl.addShaderFromCode(R"(#version 430
			in vec4 gl_FragCoord;
			
			layout(location = 0) out vec4 color;
			layout(location = 1) out float depth;
			
			uniform mat4 projection; //from local space to screen
			uniform uvec2 windowSize;
			
			uniform vec3 rightDir, topDir;
			
			vec3 background(const vec3 dir) {
				const float t = 0.5 * (dir.y + 1.0);
				return (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
			}
			void main() {
				const vec2 coord = (gl_FragCoord.xy - windowSize.xy / 2) * 2 / windowSize.xy;
		
				const vec3 forwardDir = cross(topDir, rightDir);
				const vec3 rayDir_ = rightDir * coord.x / projection[0].x + topDir * coord.y / projection[1].y + forwardDir;
				const vec3 rayDir = normalize(rayDir_);
	
				color = vec4(background(rayDir), 1.0);
				depth = gl_FragCoord.z;
			}
		
			)",
			GL_FRAGMENT_SHADER,
			"bg fragment"
		);
		
		bgsl.attachShaders(bgProgram);
	
		glLinkProgram(bgProgram);
		glValidateProgram(bgProgram);
		glUseProgram(bgProgram);
		
	
		bgsl.deleteShaders();
		
		glUniform2ui(glGetUniformLocation(bgProgram, "windowSize"), windowSize.x, windowSize.y);
		bgProjection_u = glGetUniformLocation(bgProgram, "projection");
		bgRightDir_u = glGetUniformLocation(bgProgram, "rightDir");
		bgTopDir_u = glGetUniformLocation(bgProgram, "topDir");
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
	
	glUseProgram(bgProgram);
	glUniformMatrix4fv(bgProjection_u, 1, GL_TRUE, &projection[0][0]);
	 
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

void genTrees(vec3i const chunk, Chunks::ChunkData &data, vec3i start, vec3i end, Chunks::AABB &aabb) {	
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
	
	aabb = Chunks::AABB(start, end);
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
		int neighbourIndex = -1;
		auto const neighbourPos{ pos + offset };
		for(auto const elChunkIndex : chunks.used)
			if(chunks.chunksPos[elChunkIndex] == neighbourPos) { neighbourIndex = elChunkIndex; break; }
		
		if(neighbourIndex == -1) neighbours[i] = Chunks::OptionalNeighbour();
		else {
			neighbours[i] = Chunks::OptionalNeighbour(neighbourIndex);
			chunks[neighbourIndex].neighbours()[Chunks::Neighbours::mirror(i)] = index;
			chunks[neighbourIndex].gpuPresent() = false;
		}
	}
	
	neighbours_ = neighbours;
	
	double heights[Chunks::chunkDim * Chunks::chunkDim];
	for(int z = 0; z < Chunks::chunkDim; z++) 
	for(int x = 0; x < Chunks::chunkDim; x++) {
		heights[z* Chunks::chunkDim + x] = heightAt(vec2i{pos.x,pos.z}, vec2i{x,z});
	}
	vec3i start{15};
	vec3i end  {0 };
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
		
	genTrees(pos, data, start, end, aabb);
}

void generateChunkData(int32_t const chunkIndex) {
	generateChunkData( Chunks::Chunk{ chunks, chunkIndex } );
}

static int32_t genChunkAt(vec3i const position) {
	int32_t const usedIndex{ chunks.reserve() };
	auto const chunkIndex{ chunks.used[usedIndex] };
					
	chunks.chunksPos[chunkIndex] = position;
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
	
	vec3i playerChunk{ playerCoord().chunk() };
	
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
			auto const &neighbours{ chunks[chunkIndex].neighbours() };
			for(int i{}; i < Chunks::Neighbours::neighboursCount; i++) {
				if(Chunks::Neighbours::isSelf(i)) continue;
				auto const &optNeighbour{ neighbours[i] };
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
	ChunkCoord const relativeBlockCoord{ ChunkCoord{ blockChunk, ChunkCoord::Block{blockCoord} } - playerCoord_ };
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
	
    auto& currentViewport = viewport_current();
	auto &player{ playerCoord() };

    vec2<double> diff = (mousePos - pmousePos) / windowSize_d;
	
	if(diffBlockMs >= blockActionCD * 1000 && blockAction != BlockAction::NONE && !isFreeCam) {
		bool isAction = false;

		ChunkCoord const viewport{ playerCoord_ + ChunkCoord::Fractional{ChunkCoord::posToFracTrunk(viewportOffset_)} };
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
	
	vec3d input{};
    if (isZoomMovement && isFreeCam) {
        zoomMovement += diff.x;

        const auto forwardDir = currentViewport.forwardDir();
		input += forwardDir * zoomMovement * size;
    }
    if (isPan || mouseCentered) {
        currentViewport.rotation += diff * (2 * misc::pi) * vec2d{ 0.8, 0.8 };
        currentViewport.rotation.y = misc::clamp(currentViewport.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
    }
    if (movementDir.lengthSquare() != 0) {
		vec3<double> movement;
		if(isFreeCam) {
			double projection[3][3];
			currentViewport.localToGlobalSpace(&projection);
			
			movement = vecMult(projection, movementDir);
		}
		else {
			movement = currentViewport.flatForwardDir()*movementDir.z + currentViewport.flatTopDir()*movementDir.y + currentViewport.flatRightDir()*movementDir.x;
		}
		
		input += movement.normalized() * movementSpeed 
				* (shift ? 1.0*speedModifier : 1)
				* (ctrl  ? 1.0/speedModifier : 1);
    }
	
	if(jump && isOnGround && !isFreeCam) input+=vec3d{0,1,0}*16;
	
	input *= deltaTime;
	
	if(isFreeCam) spectatorCoord += input;
	else {
		playerForce = playerForce.applied([&](double const coord, auto const index) -> double { 
			return misc::clamp(coord + input[index], fmin(coord, input[index]), fmax(coord, input[index]));
		});
	}
	
	if(diffPhysicsMs > fixedDeltaTime * 1000) {
		lastPhysicsUpdate += std::chrono::milliseconds(static_cast<long long>(fixedDeltaTime*1000.0));
		
		if(!debugBtn0) {
			playerForce+=vec3d{0,-1,0} * fixedDeltaTime;
			isOnGround = false;
			updateCollision(playerCoord_, playerForce, isOnGround);
		}
	}
	
	loadChunks();

    pmousePos = mousePos;
}

int main(void) {			
    GLFWwindow* window;

    if (!glfwInit())
        return -1;

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
	
    fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));

    //callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
	glfwSetCursorPos(window, 0, 0);
	cursor_position_callback(window, 0, 0);
	pmousePos = mousePos;

	//load shaders
	reloadShaders();
	
	loadChunks();
	
	glUseProgram(mainProgram);
	
	
	GLuint chunksVB;
	static size_t chunksVBSize = 0;
	glGenBuffers(1, &chunksVB);

	GLuint chunksVA;
	glGenVertexArrays(1, &chunksVA);
	glBindVertexArray(chunksVA);
		glBindBuffer(GL_ARRAY_BUFFER, chunksVB);
		
		glEnableVertexAttribArray(0);

		glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), (void*)( 0 )); //chunkIndex

		glVertexAttribDivisor(0, 1);
	glBindVertexArray(0); 
	
	
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
		
		auto &player{ playerCoord() };
		auto &currentViewport{ viewport_current() };
		auto const position{ player.position()+viewportOffset() };
        auto const rightDir{ currentViewport.rightDir() };
        auto const topDir{ currentViewport.topDir() };
        auto const forwardDir{ currentViewport.forwardDir() };
		
		//glClear(GL_COLOR_BUFFER_BIT);
		glClear(GL_DEPTH_BUFFER_BIT);
		
		glUseProgram(bgProgram);
		glUniform3f(bgRightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(bgTopDir_u, topDir.x, topDir.y, topDir.z);
		
		glDepthMask(GL_FALSE);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
		glDepthMask(GL_TRUE);
		
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
		
		
		float toLoc[3][3];
		float toGlob[3][3];
		currentViewport.localToGlobalSpace(&toGlob);
		currentViewport.globalToLocalSpace(&toLoc);
		//vec3i const playerChunk{ player.chunk() };
		ChunkCoord const cameraCoord{ player + viewportOffset() };
		auto const cameraChunk{ cameraCoord.chunk() };
		auto const cameraPosInChunk{ cameraCoord.positionInChunk() };
		
		if(testInfo) {
			std::cout << position << '\n';
			std::cout.precision(17);
			std::cout << playerCoord().positionInChunk() << '\n';
			std::cout.precision(2);
			std::cout << playerCoord().blockInChunk() << '\n';
			std::cout << playerCoord().chunk() << '\n';
			std::cout << "----------------------------\n";
		}
		
		float const toLoc4[4][4] = {
			{ toLoc[0][0], toLoc[0][1], toLoc[0][2], 0 },
			{ toLoc[1][0], toLoc[1][1], toLoc[1][2], 0 },
			{ toLoc[2][0], toLoc[2][1], toLoc[2][2], 0 },
			{ 0          , 0          , 0          , 1 },
		};
		glUniformMatrix4fv(toLocal_matrix_u, 1, GL_TRUE, &toLoc4[0][0]);
		glProgramUniformMatrix4fv(debugProgram, db_toLocal_u, 1, GL_TRUE, &toLoc4[0][0]);
	
		struct __attribute__ ((packed)) ChunkInfo {
			int32_t chunkIndex;
		};
				
		static std::vector<ChunkInfo> chunksInfo{};
		
		chunksInfo.clear();
		chunksInfo.reserve(chunks.used.size());
		
		static bool keep = true;
		static vec3i lastPlayerChunk;
		static vec3d lastCameraPosInChunk;
		static Viewport lastViewport;
		if(debugBtn1 && false) {
			if(keep == false) std::cout << "~" << debugBtn0 << "~\n";
			keep = true;
		}
		else keep = false;
		
		if(!keep) {
			lastPlayerChunk = cameraChunk;
			lastCameraPosInChunk = cameraPosInChunk;
			lastViewport = currentViewport;
		}
		
		float projection[4][4];
		lastViewport.projectionMatrix(&projection);
		
		float const fov( lastViewport.fov );

		vec3f const top( lastViewport.topDir() );
		vec3f const right( lastViewport.rightDir() );
		vec3f const forward( lastViewport.forwardDir() );
		
		auto const toGlobal = [&](vec2f const coord) -> vec3f {
			return right * coord.x / projection[0][0] + top * coord.y / projection[1][1] + forward;
		};
	
		vec3f const right_{ toGlobal(vec2f{+1,0}) };
		vec3f const left_ { toGlobal(vec2f{-1,0}) };
		vec3f const up_   { toGlobal(vec2f{0,+1}) };
		vec3f const down_ { toGlobal(vec2f{0,-1}) };
		
		vec3f const leftN{ left_.cross(top) };
		vec3f const rightN{ -right_.cross(top) };
		vec3f const topN{ up_.cross(right) };
		vec3f const botN{ -down_.cross(right) };
		
		vec3f const nearPos{ forward * lastViewport.near };
		vec3f const farPos{ forward * lastViewport.far };
		
		vec3f const normals[] = {
			leftN, rightN, topN, botN
		};
		
		glUniform3i(playerChunk_u, cameraChunk.x, cameraChunk.y, cameraChunk.z);
		glUniform3f(playerInChunk_u, cameraPosInChunk.x, cameraPosInChunk.y, cameraPosInChunk.z);
		
		glProgramUniform3i(debugProgram, db_playerChunk_u, cameraChunk.x, cameraChunk.y, cameraChunk.z);
		glProgramUniform3f(debugProgram, db_playerInChunk_u, cameraPosInChunk.x, cameraPosInChunk.y, cameraPosInChunk.z);
		
		for(auto const chunkIndex : chunks.used) {
			Chunks::AABB const aabb{ chunks.chunksAABB[chunkIndex] };
				
			uint32_t const aabbData{ aabb.getData() };
			vec3i const b1{ aabb.start() };
			vec3i const b2{ aabb.onePastEnd() };
			
			//if((b2 <= b1).any()) continue;
				
			auto const chunkPos{ chunks.chunksPos[chunkIndex] };
			vec3<int32_t> const relativeChunkPos_{ chunkPos-cameraChunk };
			vec3<float> const relativeChunkPos{ 
				static_cast<decltype(cameraPosInChunk)>(relativeChunkPos_)*Chunks::chunkDim
				-cameraPosInChunk
			};

			vec3d const relativeCameraPos{ cameraPosInChunk - vec3d(relativeChunkPos_*Chunks::chunkDim) };
			
			auto const nearest_CameraPos { relativeCameraPos.clamp(vec3d(b1), vec3d(b2)) };
			bool const isInChunk{ 
				nearest_CameraPos.inX(vec3d(b1), vec3d(b2)).all()
				|| nearest_CameraPos.distance(relativeCameraPos) <= 5*currentViewport.near
			};
			
			vec3<int32_t> const relativeChunkPos_2{ chunkPos-lastPlayerChunk };
			vec3<float> const relativeChunkPos2{ 
				static_cast<decltype(lastCameraPosInChunk)>(relativeChunkPos_2)*Chunks::chunkDim
				-lastCameraPosInChunk
			};
			
			auto const checkChunkuOutside = [&normals](
				vec3f const forwardDir, 
				vec3f const nearPos, vec3f const farPos, 
				vec3f const b1, vec3f const b2
			) -> bool {
				vec3f verts[8];
				{ //fill verts and test near/far
					bool anyInside = false;
					for(int i{0}; i < 8; ++i) {
						verts[i] = vec3lerp(b1, b2, vec3f(i&1, (i>>1)&1, (i>>2)&1));
						
						if((verts[i]-nearPos).dot(forwardDir) >= 0 && (verts[i]-farPos).dot(-forwardDir) >= 0) {
							anyInside = true;
						}
					}
					if(!anyInside) return true;
				}
				{ //test sides
					for(int i{}; i < int( sizeof(normals) / sizeof(normals[0]) ); i++) {
						bool allVertsOutside{true};
						for(int j{}; j < 8; j++) {
							auto const pos{ verts[j] };
							
							if(pos.dot(normals[i]) >= 0) { allVertsOutside = false; break; }
						}
						if(allVertsOutside) return true;
					}
				}
				
				return false;
			};
			
			//if(!isInChunk && checkChunkuOutside(
			//	forward,
			//	nearPos, farPos, 
			//	relativeChunkPos2 + vec3f(b1), relativeChunkPos2 + vec3f(b2)
			//)) continue;
			
			
			std::vector<bool>::reference gpuPresent = chunks.gpuPresent[chunkIndex];
			if(!gpuPresent && chunkIndex >= gpuChunksCount) resizeBuffer(); 
				
			{				
				ChunkInfo const ci{ 
					chunkIndex
				};
				
				chunksInfo.push_back(ci);
			}
		}
		
		{
			auto const chunksCount = chunksInfo.size();
			
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			
			ChunkInfo const  *zeroChunk = NULL;
			
			for(auto const &chunkInfo : chunksInfo) {
				auto const chunkIndex{ chunkInfo.chunkIndex };
				auto &chunkData{ chunks.chunksData[chunkIndex] };
				std::vector<bool>::reference gpuPresent = chunks.gpuPresent[chunkIndex];
				
				vec3i const chunkPosition{ chunks[chunkIndex].position() };
				if(chunkPosition == cameraChunk) zeroChunk = &chunkInfo;
				
				if(!gpuPresent) {
					if(chunkIndex >= gpuChunksCount) {
						std::cout << "Error: gpu buffer was not properly resized. size=" << gpuChunksCount << " expected=" << (chunkIndex+1);
						exit(-1);
					}
					uint32_t const aabbData{ chunks[chunkIndex].aabb().getData() };
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
					
					static_assert(sizeof(neighbours) == sizeof(int32_t) * 27);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunksNeighbours_ssbo); 
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(int32_t) * 27 * chunkIndex, 27 * sizeof(uint32_t), &neighbours);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				} 
			}
			
			if(zeroChunk != NULL) {
				glBindBuffer(GL_ARRAY_BUFFER, chunksVB);
				if(chunksVBSize < sizeof(ChunkInfo) * chunksCount)
					glBufferData   (GL_ARRAY_BUFFER,    sizeof(ChunkInfo) * 1, zeroChunk, GL_DYNAMIC_DRAW);
				else                                                        
					glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(ChunkInfo) * 1, zeroChunk);
				
				//glBindVertexArray(chunksVA);
				//glDrawArraysInstanced(GL_TRIANGLES, 0, 36, chunksCount);
				//	glDrawArrays(GL_TRIANGLES, 0, 36);
				//glBindVertexArray(0); 
				
				glUniform1i(isInChunk_u, 1);
				glBindVertexArray(chunksVA);
					glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				glBindVertexArray(0); 
				glUniform1i(isInChunk_u, 0);
			}
			
			if(debug) {
				glUseProgram(debugProgram);
				glUniform1i(db_isInChunk_u, 0);
				
				glDisable(GL_CULL_FACE);
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
				
				glBindVertexArray(chunksVA);
				glDrawArraysInstanced(GL_TRIANGLES, 0, 36, chunksCount);
				glBindVertexArray(0); 
				
				glEnable(GL_CULL_FACE);
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
				
				glUseProgram(mainProgram);
			}
		}
		
		/*if(testInfo) {
			Chunks::Move_to_neighbour_Chunk c{ chunks, playerChunk };
			
			std::cout << "chunks: ";
			auto c1 = c.optChunk();
			if(c1) {
				std::cout << chunks[c1.get()].position() << '\n';
				
				c1 = c.offset(vec3i{-1,0,0}, 20);
				
				if(c1) {
					std::cout << chunks[c1.get()].position() << '\n';
				
					c1 = c.offset(vec3i{0,0,-1}, 21);
					
					if(c1) {
						std::cout << chunks[c1.get()].position() << '\n';
				
						c1 = c.offset(vec3i{1,0,0}, 22);
						
						if(c1) {
							std::cout << chunks[c1.get()].position() << '\n';
				
							c1 = c.offset(vec3i{0,0,1}, 23);
							
							if(c1) {
								std::cout << chunks[c1.get()].position() << '\n';
							}
						}
					}
				}
			}	
		}*/
		
		if(isFreeCam){
			auto const playerRelativePos{ vec3f((playerCoord_ - player).position()) };
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
