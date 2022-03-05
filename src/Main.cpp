#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include <GLEW/glew.h>
#pragma clang diagnostic pop

#include <GLFW/glfw3.h>

#include"Vector.h"
#include"Chunks.h"
#include"ChunkCoord.h"
#include"Viewport.h"

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

static double movementSpeed{ 6 };
static bool jump{ false };

static bool isOnGround{false};
static vec3d playerForce{};
static ChunkCoord playerCoord_{ vec3i{0,0,0}, vec3d{0.01,12.001,0.01} };
static ChunkCoord spectatorCoord{ playerCoord_ };
static Viewport playerViewport{ 
	vec2d{ misc::pi / 2.0, 0 },
	windowSize_d.y / windowSize_d.x,
	90.0 / 180.0 * misc::pi,
	0.001,
	200
};

static double const height{ 1.95 };
static double const radius{ 0.45 };

static int64_t const rad_i{ ChunkCoord::posToFracRAway(radius).x }; 
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

static double const blockActionCD{ 100.0 / 1000.0 };


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
		
	if (key == GLFW_KEY_KP_0 && isPress) 
			debugBtn0 = !debugBtn0;
	if (key == GLFW_KEY_KP_1 && isPress) 
		debugBtn1 = !debugBtn0;	

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
}

static GLuint rightDir_u;
static GLuint topDir_u;
static GLuint near_u, far_u;
static GLuint time_u;
static GLuint mouseX_u, mouseY_u;
static GLuint relativeChunkPos_u;
static GLuint projection_u, db_projection_u, model_matrix_u, db_model_matrix_u;
static GLuint chunk_u;

static GLuint debugProgram2, db2_projection_u, db2_model_matrix_u;

static GLuint chunkIndex_u;
static GLuint blockSides_u;

static GLuint bgProjection_u;
static GLuint bgRightDir_u;
static GLuint bgTopDir_u;

static GLuint mainProgram = 0;
static GLuint debugProgram = 0;
static GLuint bgProgram = 0;
static GLuint playerProgram = 0;

static GLuint pl_projection_u = 0;
static GLuint pl_modelMatrix_u = 0;

static int32_t gpuChunksCount = 0;
Chunks chunks{};

void resizeBuffer(int32_t newGpuChunksCount) {
	assert(newGpuChunksCount >= 0);
	gpuChunksCount = newGpuChunksCount;
	auto &it = chunks.gpuPresent();
	for(auto &&el : it) {
		el = false;
	}
	
	static_assert(sizeof(chunks.chunksData()[0]) == 8192);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkIndex_u);
	glBufferData(GL_SHADER_STORAGE_BUFFER, gpuChunksCount * 8192, NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, chunkIndex_u);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void reloadShaders() {
	{
		glDeleteProgram(mainProgram);
		mainProgram = glCreateProgram();
		ShaderLoader sl{};
	
		//sl.addShaderFromProjectFileName("shaders/vertex.shader",GL_VERTEX_SHADER,"main vertex");
		//https://gist.github.com/rikusalminen/9393151
		//... usage: glDrawArrays(GL_TRIANGLES, 0, 36), disable all vertex arrays
		sl.addShaderFromCode(
			R"(#version 420
			uniform mat4 projection;
			uniform mat4 model_matrix;
			
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
				xyz = (xyz + 1) / 2 * 16;
			
				gl_Position = projection * (model_matrix * vec4(xyz, 1.0));
			}
			)",
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
		model_matrix_u = glGetUniformLocation(mainProgram, "model_matrix");
		chunk_u = glGetUniformLocation(mainProgram, "chunk");
		
		//images
		GLuint textures[1];
		char const* (paths)[1]{ "assets/atlas.bmp"
		};
		loadGLImages<1>(textures, paths);
	
		GLuint const atlasTex_u = glGetUniformLocation(mainProgram, "atlas");
	
		glUniform1i(atlasTex_u, 0);
	
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures[0]);
	

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
		
		relativeChunkPos_u = glGetUniformLocation(mainProgram, "relativeChunkPos");
		
		glGenBuffers(1, &chunkIndex_u);
		resizeBuffer(0);
		}
		
		{
		glDeleteProgram(bgProgram);
		bgProgram = glCreateProgram();
		ShaderLoader bgsl{};
		bgsl.addScreenSizeTriangleStripVertexShader("bg vertex");
		bgsl.addShaderFromCode(R"(#version 430
			in vec4 gl_FragCoord;
			out vec4 color;
			
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
		glDeleteProgram(debugProgram);
		debugProgram = glCreateProgram();
		ShaderLoader dbsl{};
	
		//sl.addShaderFromProjectFileName("shaders/vertex.shader",GL_VERTEX_SHADER,"main vertex");
		//https://gist.github.com/rikusalminen/9393151
		//... usage: glDrawArrays(GL_TRIANGLES, 0, 36), disable all vertex arrays
		dbsl.addShaderFromCode(R"(#version 420
			uniform mat4 projection;
			uniform mat4 model_matrix;
			
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
				xyz = (xyz + 1) / 2 * 16;
			
				gl_Position = projection * (model_matrix * vec4(xyz, 1.0));
			}
		)", GL_VERTEX_SHADER, "debug vertex");
		dbsl.addShaderFromCode(
			R"(#version 420
			out vec4 color;
			void main() {
				color = vec4(1, 0, 0, 1);
			})",
			GL_FRAGMENT_SHADER, "debug shader"
		);
	
		dbsl.attachShaders(debugProgram);
	
		glLinkProgram(debugProgram);
		glValidateProgram(debugProgram);
	
		dbsl.deleteShaders();
	
		glUseProgram(debugProgram);
		
		db_model_matrix_u = glGetUniformLocation(debugProgram, "model_matrix");
		db_projection_u = glGetUniformLocation(debugProgram, "projection");
	}
	
	{
		glDeleteProgram(debugProgram2);
		debugProgram2 = glCreateProgram();
		ShaderLoader dbsl{};

		dbsl.addShaderFromCode(R"(#version 420
			layout(location = 0) in vec3 pos;
			
			uniform mat4 projection;
			uniform mat4 model_matrix;
			
			void main() {			
				gl_Position = projection * (model_matrix * vec4(pos, 1.0));
			}
		)", GL_VERTEX_SHADER, "debug2 vertex");
		dbsl.addShaderFromCode(
			R"(#version 420
			out vec4 color;
			void main() {
				color = vec4(1, 0, 0, 1);
			})",
			GL_FRAGMENT_SHADER, "debug2 shader"
		);
	
		dbsl.attachShaders(debugProgram2);
	
		glLinkProgram(debugProgram2);
		glValidateProgram(debugProgram2);
	
		dbsl.deleteShaders();
	
		glUseProgram(debugProgram2);
		
		db2_model_matrix_u = glGetUniformLocation(debugProgram2, "model_matrix");
		db2_projection_u = glGetUniformLocation(debugProgram2, "projection");
	}
	
	
	{
		glDeleteProgram(playerProgram);
		playerProgram = glCreateProgram();
		ShaderLoader plsl{};

		plsl.addShaderFromCode(
			R"(#version 420
			uniform mat4 projection;
			uniform mat4 modelMatrix;
			
			layout(location = 0) in vec3 pos_;
			layout(location = 1) in vec3 norm_;
			
			out vec3 norm;
			
			void main() {
				gl_Position = projection * (modelMatrix * vec4(pos_, 1));
				norm = norm_;
			})", GL_VERTEX_SHADER, "Player vertex"
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
		pl_modelMatrix_u = glGetUniformLocation(playerProgram, "modelMatrix");
	}
	
	float projection[4][4];
	viewport_current().projectionMatrix(&projection);
	
	glUseProgram(playerProgram);
	glUniformMatrix4fv(pl_projection_u, 1, GL_TRUE, &projection[0][0]);
	
	glUseProgram(bgProgram);
	glUniformMatrix4fv(bgProjection_u, 1, GL_TRUE, &projection[0][0]);
	
	glUseProgram(debugProgram);
	glUniformMatrix4fv(db_projection_u, 1, GL_TRUE, &projection[0][0]);	
	
	glUseProgram(debugProgram2);
	glUniformMatrix4fv(db2_projection_u, 1, GL_TRUE, &projection[0][0]);
	
	glUseProgram(mainProgram);
	glUniformMatrix4fv(projection_u, 1, GL_TRUE, &projection[0][0]);
}

template<typename T, typename L>
inline void apply(size_t size, T &t, L&& l) {
	for(size_t i = 0; i < size; ++i) 
		if(l(t[i], i) == true) break;
}

siv::PerlinNoise perlin{ (uint32_t)rand() };
static int viewDistance = 3;

void generateChunk(vec3<int> const pos, Chunks::ChunkData &data) {
	for(int y = 0; y < Chunks::chunkDim; ++y) 
		for(int z = 0; z < Chunks::chunkDim; ++z)
			for(int x = 0; x < Chunks::chunkDim; ++x) {
				auto const value = perlin.octave2D(
					(pos.x * Chunks::chunkDim + x) / 20.0, 
					(pos.z * Chunks::chunkDim  + z) / 20.0, 
					3
				);
				auto const height = misc::map<double>(value, -1, 1, 5, 15);
				auto const index{ Chunks::blockIndex(vec3<int32_t>{x, y, z}) };
				//if(misc::mod(int32_t(height), 9) == misc::mod((pos.y * Chunks::chunkDim + y + 1), 9)) { //repeated floor
				double const diff{ height - double(pos.y * Chunks::chunkDim + y) };
				if(diff >= 0) {
					//data[index] = 1;
					uint16_t block = 0;
					if(diff < 1) block = 1; //grass
					else if(diff < 5) block = 2; //dift
					else block = 6; //stone
					data[index] = block;
				}
				else {
					data[index] = 0;
				}
			}
}

static int32_t genChunkAt(vec3i const position) {
	int32_t const usedIndex{ chunks.reserve() };
	auto const chunkIndex{ chunks.usedChunks()[usedIndex] };
					
	chunks.chunksPosition()[chunkIndex] = position;
	chunks.gpuPresent()[chunkIndex] = false;
	generateChunk(position, chunks.chunksData()[chunkIndex]);
	
	return usedIndex;
}

static void loadChunks() {
	static std::vector<int8_t> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.resize(viewWidth*viewWidth*viewWidth);
	for(size_t i = 0; i != chunksPresent.size(); i++)
		chunksPresent[i] = -1;
	
	vec3i playerChunk{ playerCoord().chunk() };
	
	vec3<int32_t> b{viewDistance, viewDistance, viewDistance};
	
	chunks.filterUsed([&](int chunkIndex) -> bool { 
		auto const relativeChunkPos = chunks.chunksPosition()[chunkIndex] - playerChunk;
		if(relativeChunkPos.in(-b, b).all()) {
			auto const index2 = relativeChunkPos + b;
			chunksPresent[index2.x + index2.y * viewWidth  + index2.z * viewWidth * viewWidth] = 1;
			
			return true;
		} 
		return false;
	});

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
	vec3d posInChunk;
	vec3l chunkPart;
	vec3i chunk;
	
	vec3d line;
	vec3d dir;
	vec3d stepLength;
	double lineLen;
	
	PosDir(ChunkCoord const coord, vec3d line_): 
		posInChunk{ coord.positionInChunk() },
		chunkPart{ coord.chunkPart__long() },
		chunk{ coord.chunk() },
		
		line{ line_ },
		dir{ line_.normalized() },
		stepLength{ vec3d{1.0} / dir.abs() },
		lineLen{ line_.length() }
	{}	  
	
	vec3l part_end() const { return chunkPart + ChunkCoord::posToFracTrunk(line); }
	ChunkCoord end() const { return ChunkCoord{ chunk, ChunkCoord::Fractional{part_end()} }; }
	
	vec3l part_at(double const t) const { return chunkPart + ChunkCoord::posToFracTrunk(dir * t); }
	ChunkCoord at(double const t) const { return ChunkCoord{ chunk, ChunkCoord::Fractional{part_at(t)} }; }
};

struct CollisionInfo {
	vec3i blockCoord;
	vec3i normals;
	
	PosDir const *posDir;
	
	double radius;
	double height;
	double maxDist;
};

struct CollisionResult {
	vec3l pos;
	vec3d force;
	double distance;
	bool isCollision;
	
	static CollisionResult noCollision() {
		return CollisionResult{};
	}
	
	friend std::ostream &operator<<(std::ostream &o, CollisionResult const &it) {
		return o  << '(' << it.pos << ';' << it.force << ';' << it.distance << ';' << it.isCollision << ')';
	}
};

static CollisionResult checkSide(CollisionInfo const ci, vec3b const forwardAxis, vec3b const rightAxis, vec3b const upAxis) {
	vec3i const fi{ forwardAxis };
	vec3l const fl{ forwardAxis };
	vec3d const fd{ forwardAxis };
	vec3i const ri{ rightAxis };
	vec3l const rl{ rightAxis };
	vec3d const rd{ rightAxis };
	vec3d const ud{ upAxis };
	vec3l const ul{ upAxis };
	auto const &pd{ *ci.posDir };
	
	vec3i const facesCoord_i{ ci.blockCoord + ci.normals.max(0) };
	vec3d const localFacesCoord{ vec3d(facesCoord_i) - pd.posInChunk };
	auto  newT = (localFacesCoord.dot(fd)*pd.dir.sign().dot(fi) - radius) * pd.stepLength.dotNonan(fd);
	vec3d normal{ ci.normals * fi };
	if(normal == 0) return CollisionResult::noCollision();
	vec3l v{ pd.chunkPart + ChunkCoord::posToFracTrunk(pd.dir * newT) };
	
	bool const checkRight = v.in(ChunkCoord::blockToFrac(ci.blockCoord), ChunkCoord::blockToFrac(ci.blockCoord+1)).dot(rightAxis);
	if(!checkRight) {
		vec2i minEdge;
		double minDist, minT;
		
		vec2d flatBotInChunk{ pd.posInChunk.dot(fd), pd.posInChunk.dot(rd) };
		vec2d flatDir{ vec2d{pd.dir.dot(fd), pd.dir.dot(rd)}.normalized() };
		
		vec2i const edge1{ facesCoord_i.dot(fi), ci.blockCoord.dot(ri)+0 };
		vec2i const edge2{ facesCoord_i.dot(fi), ci.blockCoord.dot(ri)+1 };
		vec2d const toEdge1{ static_cast<vec2d>(edge1) - flatBotInChunk };
		vec2d const toEdge2{ static_cast<vec2d>(edge2) - flatBotInChunk };
		
		
		auto const minDist1{ abs(flatDir.cross(toEdge1)) };
		auto const minDist2{ abs(flatDir.cross(toEdge2)) };
		
		auto const minT1{ flatDir.dot(toEdge1) };
		auto const minT2{ flatDir.dot(toEdge2) };
		
		if(minDist1 < minDist2) { minT = minT1; minEdge = edge1; minDist = minDist1; }
		else					{ minT = minT2; minEdge = edge2; minDist = minDist2; }
		
		if(minDist > radius || isnan(minDist)) return CollisionResult::noCollision();
		
		double const dt{sqrt(radius*radius - minDist*minDist)};
		if(std::isnan(dt)) {
			std::cout.precision(17);
			std::cerr << "err:" << radius << ' ' << minDist << '\n';
			assert(false);
		}
		
		newT = minT - dt;
		
		v = pd.part_at(newT);
		vec2l const newToMinEdge{ ChunkCoord::blockToFrac(minEdge) - vec2l{v.dot(fl), v.dot(rl)} };
		
		vec3l const newToMinEdgeAbs{ fi * newToMinEdge.x + ri * newToMinEdge.y };
		normal = -ChunkCoord::fracToPos(newToMinEdgeAbs).normalized();
		
		/*vec3d end = pd.posInChunk + pd.dir * newT;
		
		vec2d const newToMinEdge{ vec2d(minEdge) - vec2d{end.dot(fd), end.dot(rd)} };
		
		vec3d const newToMinEdgeAbs{ fd * newToMinEdge.x + rd * newToMinEdge.y };
		normal = -vec3d{ newToMinEdgeAbs }.normalized();
		
		auto const offsetAbs{ ChunkCoord::posToFracRAway(normal * radius)  };
		auto const minEdgeAbs{ fi * minEdge.x + ri * minEdge.y };
		v = ChunkCoord::posToFrac(end * ud) + ChunkCoord::blockToFrac(minEdgeAbs) + offsetAbs;*/
	}
	
	int64_t const blockPos{ ChunkCoord::blockToFrac(ci.blockCoord).dot(ul) };
	bool const checkU = misc::intersectsX( 
		blockPos, blockPos+ChunkCoord::blockToFrac(vec3i{1}).x,
		 v.dot(ul), (v+height_i).dot(ul)
	);
		//ChunkCoord::blockToFrac(ci.blockCoord).dot(ul) < (v+height_i).dot(ul)
		//&& 
		//(ChunkCoord::blockToFrac(ci.blockCoord+1).dot(ul) > v.dot(ul));
	
	double const strengthF{ static_cast<vec3d>(normal).dot(-pd.line) };
	if(checkU && strengthF >= 0) {
		return CollisionResult {
			v,
			pd.line + normal * strengthF,
			newT,
			true
		};
		//newForce = playerForce + normal * 2 * strengthF;
	}
	return CollisionResult::noCollision();
}

static CollisionResult checkTB(CollisionInfo const ci, bool top, vec3b const forwardAxis, vec3b const rightAxis, vec3b const upAxis) {
	vec3d  const fd{ forwardAxis };
	vec3d  const rd{ rightAxis };
	vec3d  const ud{ upAxis };
	auto const &pd{ *ci.posDir };
	
	vec3d const checkPos{ top ? (pd.posInChunk + fd * ci.height) : pd.posInChunk };
	
	vec3i const facesCoord_i{ ci.blockCoord + ci.normals.max(0) };
	vec3d const localFacesCoord{ vec3d(facesCoord_i) - checkPos };
	double const distToSide{( localFacesCoord * pd.stepLength * vec3d(pd.dir.sign()) ).dotNonan(fd)};

	vec3d const normal{ vec3d(ci.normals) * fd };
	if(normal == 0) return CollisionResult::noCollision();
	double const strength{ normal.dot(-pd.line) };
	
	if(strength > 0) { 
		vec3d const coordAtSide{ checkPos + pd.dir*distToSide };
		vec3d const clp{ coordAtSide.clamp(vec3d(ci.blockCoord), vec3d(ci.blockCoord)+1.0) };
		vec2d const closestPoint{
			clp.dot(rd),
			clp.dot(ud)
		};

		bool isIntersection{ (vec2d{coordAtSide.x, coordAtSide.z}-closestPoint).lengthSquare() < radius*radius };
		
		return CollisionResult{
			pd.part_at(distToSide),
			(pd.line + normal * strength) * vec3lerp(vec3d{0.8}, vec3d{1}, fd),
			distToSide,
			isIntersection
		};
	}
	return CollisionResult::noCollision();
}

struct DDA {
private:
	//const
	PosDir posDir;
	vec3i firstCellRow;
	vec3d firstCellDiff;
	double maxLen;
	
	//mutable
	vec3i curSteps;
	bool end;
public:
	DDA(PosDir const pd_) : 
	    posDir{ pd_ },
		firstCellRow{ vec3l((pd_.posInChunk + vec3d(pd_.dir.sign().max(vec3i{0}))).floor()) },
		firstCellDiff{ ( pd_.posInChunk - vec3d(firstCellRow) ).abs() }, //distance to the frist border 
		maxLen{ pd_.line.length() },
		curSteps{ 0 },
		end{ false }
	{}
	
	std::tuple<double, vec3b, vec3i> next() {
		vec3d const curLen{ (firstCellDiff + vec3d{curSteps}) * posDir.stepLength };
		double minCurLen{ fmin(fmin(curLen.x, curLen.y), curLen.z) };
		
		if(minCurLen >= maxLen) { 
			end = true;
			minCurLen = maxLen; 
		}
		vec3b const minAxis{ curLen.equal(minCurLen) };
		
		vec3i const curSteps_{ curSteps };
		curSteps += vec3i(minAxis);
		
		return std::make_tuple( minCurLen, minAxis, curSteps_ );
	}
	
	#define G(name) auto get_##name() const { return name ; }
	G(posDir)
	G(firstCellRow)
	G(firstCellDiff)
	G(maxLen)
	G(curSteps)
	G(end)
	#undef G
};

static bool updateCollision(ChunkCoord &player, vec3d &playerForce, bool &isOnGround, std::vector<vec3i> &candidates, vec3i *(&collided)) {	
	static int prev = 0;
		
	PosDir const posDir{ player, playerForce };
	DDA dda{ posDir };
	
	vec3i lastChunkCoord;
	int32_t lastChunkIndex = -1;
	
	vec3l newOffset{ posDir.part_end() };
	vec3d newForce{ posDir.line };
	double newPlayerDist = dda.get_maxLen();
	bool isCollision{ false };	
	bool newIsOnGround{ isOnGround };
	
	vec3i collided_;
	
	bool side = false;
	bool skipS = false;
	bool x__ = false;
	
	vec3i lastBBMin{0};
	vec3i lastBBMax{0};
	
	for(int i = 0;; i ++) {
		double const minLen{ std::get<0>(dda.next()) };
		vec3l const curCoord = posDir.part_at(minLen);

		vec2l const botXZ_i{ curCoord.x, curCoord.z };
		
		auto const curBBMin{ ChunkCoord::fracToBlock(vec3l{botXZ_i.x - rad_i, curCoord.y         , botXZ_i.y - rad_i}) };
		auto const curBBMax{ ChunkCoord::fracToBlock(vec3l{botXZ_i.x + rad_i, curCoord.y+height_i, botXZ_i.y + rad_i}) };
		
		for(int32_t x = curBBMin.x; x <= curBBMax.x; x++)	
		for(int32_t y = curBBMin.y; y <= curBBMax.y; y++)
		for(int32_t z = curBBMin.z; z <= curBBMax.z; z++) {
			vec3i const coord{x,y,z};
			
			if(coord.in(lastBBMin, lastBBMax).all()) continue;
			
			ChunkCoord const coord_{ posDir.chunk, ChunkCoord::Block{coord} };
			vec3i chunkCoord{ coord_.chunk() };
			int chunkIndex = -1;
			
			if(lastChunkCoord == chunkCoord && lastChunkIndex != -1) chunkIndex = lastChunkIndex; //lastChunkIndex itself must not be -1 (-1 - chunk not generated yet)
			else for(auto const elChunkIndex : chunks.usedChunks())
				if(chunks.chunksPosition()[elChunkIndex] == chunkCoord) { chunkIndex = elChunkIndex; break; }
			
			lastChunkIndex = chunkIndex;
			lastChunkCoord = chunkCoord; 
			
			if(chunkIndex == -1) { 
				//auto const usedIndex{ genChunkAt(chunkCoord) }; //generates every frame
				//chunkIndex = chunks.usedChunks()[usedIndex];
				std::cout << "add chunk gen!\n"; playerForce = 0; return true; 
			}
			auto const chunkData{ chunks.chunksData()[chunkIndex] };
			auto const index{ Chunks::blockIndex(coord_.blockInChunk()) };
			uint16_t const &block{ chunkData[index] };
			if(block != 0) {				
				CollisionInfo const ci{
					coord,
					vec3i{1},
					
					&posDir,
					
					radius,
					height,
					newPlayerDist
				};
				

				
				CollisionInfo ci2{ ci };
				ci2.normals = -ci2.normals;

				
				CollisionResult collisions[] = {
					checkTB(ci, false, vec3b{0,1,0}, vec3b{1,0,0}, vec3b{0,0,1}), //y-
					checkTB(ci2, true, vec3b{0,1,0}, vec3b{1,0,0}, vec3b{0,0,1}), //y+
					checkSide(ci,      vec3b{1,0,0}, vec3b{0,0,1}, vec3b{0,1,0}), //x
					checkSide(ci2,     vec3b{1,0,0}, vec3b{0,0,1}, vec3b{0,1,0}), //x-
					checkSide(ci,      vec3b{0,0,1}, vec3b{1,0,0}, vec3b{0,1,0}), //z
					checkSide(ci2,     vec3b{0,0,1}, vec3b{1,0,0}, vec3b{0,1,0}), //z-
				};
				
				int minI = -1;
				double minDist = newPlayerDist;
				

				for(size_t i = 0; i < sizeof(collisions)/sizeof(collisions[0]); i++) {
					auto const &c{ collisions[i] };
					if(c.isCollision) {
						candidates.push_back(posDir.chunk * Chunks::chunkDim + coord);
					}
					if(c.isCollision && c.distance <= minDist && c.distance >= 0) {
						minI = i;
						minDist = c.distance;
					}
				}
				
				if(minI != -1) {
					auto &c{ collisions[minI] };
					isCollision = true;
					newIsOnGround = (minI == 0) || isOnGround;
					newPlayerDist = minDist;
					newOffset = c.pos;

					newForce = c.force;
					
					if(minI >= 2) { side = true; prev = 5; x__ = minI < 4; skipS = false; }
					else side = false;
					collided_ = posDir.chunk * Chunks::chunkDim + coord;
				}
				else {
					skipS = true;
				}
			}
		}
		
		if( i >= 100 || dda.get_end() || newPlayerDist <= minLen ) break;

		lastBBMin = curBBMin;
		lastBBMax = curBBMax;
	}
	
	player = ChunkCoord{ posDir.chunk, ChunkCoord::Fractional{newOffset} };
	playerForce = newForce;
	isOnGround = newIsOnGround;
	
	if(isCollision) *collided = collided_;
	else collided = NULL;
	return !isCollision || newForce.lengthSquare() == 0;
}

static std::vector<vec3f> positions{};
static std::vector<vec3i> candidates{}; //collision candidates
static std::vector<vec3i> collided{};

bool checkCanPlaceBlock(vec3i const blockChunk, vec3i const blockCoord) {
	ChunkCoord const relativeBlockCoord{ ChunkCoord{ blockChunk, ChunkCoord::Block{blockCoord} } - playerCoord_ };
	vec3l const relativeBlockPos{ relativeBlockCoord.position__long() };
	vec3l const relativeBlockEndPos{ relativeBlockPos + ChunkCoord::blockToFrac(vec3i{1}) };
	bool const checkY{ misc::intersectsX(relativeBlockPos.y, relativeBlockEndPos.y, 0ll, height_i) };
	if(!checkY) return true;
	vec3l const nearest{ vec3l{0}.clamp( 
		relativeBlockPos, 
		relativeBlockEndPos
	) };
	vec2l nearestXZ{ nearest.x, nearest.z };
	
	if(nearestXZ.lengthSquare() >= rad_i*rad_i) return true;
	return false;
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
		PosDir const pd{ PosDir(viewport, viewport_current().forwardDir() * 7) };
		vec3i const dirSign{ pd.dir.sign() };
		DDA checkBlock{ pd };
		
		vec3i lastChunkCoord{};
		int lastChunkIndex{-1};
	
		if(blockAction == BlockAction::BREAK) {
			for(int i = 0;; i++) {
				double at;
				vec3b minAxis;
				vec3b otherAxis;
				vec3i curSteps;
				std::tie(at, minAxis, curSteps) = checkBlock.next();
				otherAxis = !minAxis;
				
				vec3i const coord_{ ChunkCoord::fracToBlock(pd.part_at(at)) };
				ChunkCoord const coord{ 
					pd.chunk,
					ChunkCoord::Block{ 
						coord_ * vec3i(otherAxis) + 
						(checkBlock.get_firstCellRow() + dirSign * curSteps) * vec3i(minAxis) + 
						pd.dir.sign().min(0) * vec3i{minAxis} 
					} 
				};
				
				vec3i const blockCoord = coord.blockInChunk();
				vec3i const blockChunk = coord.chunk();
				
				int chunkIndex = -1;
				
				if(lastChunkCoord == blockChunk && lastChunkIndex != -1) chunkIndex = lastChunkIndex; //lastChunkIndex itself must not be -1 (-1 - chunk not generated yet)
				else for(auto const elChunkIndex : chunks.usedChunks())
					if(chunks.chunksPosition()[elChunkIndex] == blockChunk) { chunkIndex = elChunkIndex; break; }
				
				lastChunkIndex = chunkIndex;
				lastChunkCoord = blockChunk; 
				
				if(chunkIndex == -1) { 
					//auto const usedIndex{ genChunkAt(blockChunk) }; //generates every frame
					//chunkIndex = chunks.usedChunks()[usedIndex];
					std::cout << "block br: add chunk gen!" << coord << '\n'; 
					break;
				}
				
				auto const chunkData{ chunks.chunksData()[chunkIndex] };
				auto const index{ Chunks::blockIndex(coord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					auto const index{ Chunks::blockIndex(blockCoord) };
					uint16_t &block{ chunks.chunksData()[chunkIndex][index] };
					block = 0;
					chunks.gpuPresent()[chunkIndex] = false;
					isAction = true;
					break;
				}
				else if(checkBlock.get_end() || i >= 100) break;
			}
		}
		else {
			for(int i = 0;; i++) {
			
				double at;
				vec3b minAxis;
				vec3b otherAxis;
				vec3i curSteps;
				std::tie(at, minAxis, curSteps) = checkBlock.next();
				otherAxis = !minAxis;
				
				vec3i const coord_{ ChunkCoord::fracToBlock(pd.part_at(at)) };
				ChunkCoord const coord{ 
					pd.chunk,
					ChunkCoord::Block{ 
						coord_ * vec3i(otherAxis) + 
						(checkBlock.get_firstCellRow() + dirSign * curSteps) * vec3i(minAxis) + 
						pd.dir.sign().min(0) * vec3i{minAxis} 
					} 
				};
				
				ChunkCoord const nextCoord{ pd.at(at) + ChunkCoord::Block{ pd.dir.sign().min(0) * vec3i{minAxis} } };
				
				vec3i const blockCoord = nextCoord.blockInChunk();
				vec3i const blockChunk = nextCoord.chunk();
				
				int chunkIndex = -1;
				
				if(lastChunkCoord == blockChunk && lastChunkIndex != -1) chunkIndex = lastChunkIndex; //lastChunkIndex itself must not be -1 (-1 - chunk not generated yet)
				else for(auto const elChunkIndex : chunks.usedChunks())
					if(chunks.chunksPosition()[elChunkIndex] == blockChunk) { chunkIndex = elChunkIndex; break; }
				
				lastChunkIndex = chunkIndex;
				lastChunkCoord = blockChunk; 
				
				if(chunkIndex == -1) { 
					//auto const usedIndex{ genChunkAt(blockChunk) }; //generates every frame
					//chunkIndex = chunks.usedChunks()[usedIndex];
					std::cout << "block pl: add chunk gen!" << coord << '\n'; 
					break;
				}
				
				auto const chunkData{ chunks.chunksData()[chunkIndex] };
				auto const index{ Chunks::blockIndex(nextCoord.blockInChunk()) };
				uint16_t const &block{ chunkData[index] };
				
				if(block != 0) {
					ChunkCoord const bc{ coord - ChunkCoord::Block{ dirSign * vec3i{minAxis} } };
					vec3i const blockCoord = bc.blockInChunk();
					vec3i const blockChunk = bc.chunk();
			
					int chunkIndex = -1;
			
					if(lastChunkCoord == blockChunk && lastChunkIndex != -1) chunkIndex = lastChunkIndex; //lastChunkIndex itself must not be -1 (-1 - chunk not generated yet)
					else for(auto const elChunkIndex : chunks.usedChunks())
						if(chunks.chunksPosition()[elChunkIndex] == blockChunk) { chunkIndex = elChunkIndex; break; }
					
					if(chunkIndex == -1) { 
						std::cout << "block pl(2): add chunk gen!" << coord << '\n'; 
						break;
					}
				
					auto const index{ Chunks::blockIndex(blockCoord) };
					uint16_t &block{ chunks.chunksData()[chunkIndex][index] };
					
					if(checkCanPlaceBlock(blockChunk, blockCoord)) {
						block = 1;
						chunks.gpuPresent()[chunkIndex] = false;
						isAction = true;
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
	
	if(jump && isOnGround) input+=vec3d{0,1,0}*16;
	
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
			candidates.clear();
			collided.clear();
			playerForce+=vec3d{0,-1,0} * fixedDeltaTime;
			positions.clear();
			isOnGround = false;
			int i = 0;
			for(; i < 1000; i++) {
				vec3i cd{};
				vec3i *is = &cd;
				bool const br = updateCollision(playerCoord_, playerForce, isOnGround, candidates, is);
				
				if(is)collided.push_back(cd);
				if(br) break;
			}
			int r;
			if((r = (int(positions.size()) - 1000)) > 0) {
				positions.erase(positions.begin(), positions.begin() + r);
			}
		}
	}
	
	loadChunks();

    pmousePos = mousePos;
}

int main(void) {
	//vec3d a{15.55, 0, -0.45};
	//double x, y, z;
	//std::cin >> x >> y >> z;
	//std::cout << a.clamp(vec3d{x, y, z}, vec3d{x, y, z}+1);
	//exit(0);
	
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
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
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
	
	
	const int radialCount = 32;
	vec3f verts [(radialCount+1)*2*2]; //radialCount+1 - closed loop, *2 - top & bottom, *2 - pos & normal
	vec3f topCap[(radialCount+1)  *2];
	vec3f botCap[(radialCount+1)  *2];
	
	for(int i = 0; i < radialCount+1; i ++) {
		double const angle{ -i / (double)radialCount * 2.0 * misc::pi };
		
		auto const x{ (float) (cos(angle) * radius) };
		auto const y{ (float) (sin(angle) * radius) };
		
		verts[i*2*2  ] = botCap[i*2] = vec3f{ x, 0, y };
		verts[i*2*2+1] = vec3f{ x, 0, y }; //normals are not normalized 
		botCap[i*2+1] = vec3f{0,-1,0};
		
		verts[(i*2+1)*2  ] = topCap[(radialCount-i)*2] = vec3f{ x, (float) height, y };
		verts[(i*2+1)*2+1] = vec3f{ x, 0, y };
		topCap[(radialCount-i)*2+1] = vec3f{0,1,0};
	}

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	GLuint vao;
	glGenVertexArrays(1, &vao);
	
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 6, NULL); //pos
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 6, (void*)(sizeof(GLfloat)*3)); //norm
	//glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 7, (void*) (sizeof(GLfloat) * 4));

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0); 
	
	
	GLuint vb;
	glGenBuffers(1, &vb);
	glBindBuffer(GL_ARRAY_BUFFER, vb);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	GLuint va;
	glGenVertexArrays(1, &va);
	
	glBindVertexArray(va);
	glBindBuffer(GL_ARRAY_BUFFER, vb);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
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
		
		//glClear(GL_COLOR_BUFFER_BIT);
		glClear(GL_DEPTH_BUFFER_BIT);
		
		glUseProgram(bgProgram);
		glUniform3f(bgRightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(bgTopDir_u, topDir.x, topDir.y, topDir.z);
		glCullFace(GL_FRONT); 
		
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
		double toGlob[3][3];
		currentViewport.localToGlobalSpace(&toGlob);
		currentViewport.globalToLocalSpace(&toLoc);
		vec3<int32_t> const playerChunk{ player.chunk() };
		auto const cameraPosInChunk{ player.positionInChunk()+viewportOffset() };
		
		if(testInfo) {
			std::cout << position << '\n';
			std::cout.precision(17);
			std::cout << playerCoord().positionInChunk() << '\n';
			std::cout.precision(2);
			std::cout << playerCoord().blockInChunk() << '\n';
			std::cout << playerCoord().chunk() << '\n';
		}
		
		float const toLoc4[4][4] = {
			{ toLoc[0][0], toLoc[0][1], toLoc[0][2], 0 },
			{ toLoc[1][0], toLoc[1][1], toLoc[1][2], 0 },
			{ toLoc[2][0], toLoc[2][1], toLoc[2][2], 0 },
			{ 0          , 0          , 0          , 1 },
		};
		
		struct ChunkSort {
			double sqDistance;
			int chunkIndex;
		};
		std::vector<ChunkSort> chunksSorted{};
		chunksSorted.reserve(chunks.usedChunks().size());
		for(auto const chunkIndex : chunks.usedChunks()) {
			auto const chunkPos{ chunks.chunksPosition()[chunkIndex] };
			vec3<double> const relativeChunkPos{ 
				static_cast<decltype(cameraPosInChunk)>(chunkPos-playerChunk)*Chunks::chunkDim
				+(Chunks::chunkDim/2.0f)
				-cameraPosInChunk
			};
			chunksSorted.push_back({ relativeChunkPos.lengthSquare(), chunkIndex });
		}
		std::sort(chunksSorted.begin(), chunksSorted.end(), [](ChunkSort const c1, ChunkSort const c2) -> bool {
			return c1.sqDistance > c2.sqDistance; //chunks located nearer are rendered last
		});
		
		float projection[4][4];
		viewport_current().projectionMatrix(&projection);
		
		for(auto const [_ignore_, chunkIndex] : chunksSorted) {
			auto const chunkPos{ chunks.chunksPosition()[chunkIndex] };
			auto  &chunkData{ chunks.chunksData()[chunkIndex] };
			vec3<int32_t> const relativeChunkPos_{ chunkPos-playerChunk };
			vec3<float> const relativeChunkPos{ 
				static_cast<decltype(cameraPosInChunk)>(relativeChunkPos_)*Chunks::chunkDim
				-cameraPosInChunk
			};
			glUniform3f(relativeChunkPos_u, relativeChunkPos.x, relativeChunkPos.y, relativeChunkPos.z);


			glUniform1ui(chunk_u, chunkIndex);
			{ 
				std::vector<bool>::reference gpuPresent = chunks.gpuPresent()[chunkIndex];
				if(!gpuPresent) {
					if(chunkIndex >= gpuChunksCount) {
						resizeBuffer(chunkIndex+1);
					}
					gpuPresent = true;
					
					static_assert(sizeof chunkData == (8192));
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkIndex_u); 
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8192 * chunkIndex, 8192, &chunkData);
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				} 
			}
				
			/*static std::vector<size_t> fix{};
			fix.clear();
			
			for(size_t i = 0; i < candidates.size(); i ++) {
				auto cand{candidates[i]};
				auto coord{ ChunkCoord(vec3i{}, ChunkCoord::Block(cand)) };
				if(coord.chunk() == chunkPos) {
					auto index{ Chunks::blockIndex(coord.blockInChunk()) };
					chunkData[index] = 2;
					fix.push_back(index);
				}
			}
			for(size_t i = 0; i < collided.size(); i ++) {
				auto cand{collided[i]};
				auto coord{ ChunkCoord(vec3i{}, ChunkCoord::Block(cand)) };
				if(coord.chunk() == chunkPos) {
					auto index{ Chunks::blockIndex(coord.blockInChunk()) };
					chunkData[index] = 3;
					fix.push_back(index);
				}
			}
			
			glUniform1ui(chunk_u, 0);
			if(gpuChunksCount < 1) {
				resizeBuffer(1);
			}
			
			static_assert(sizeof chunkData == (8192));
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkIndex_u); 
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 8192, &chunkData);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			for(size_t i = 0; i < fix.size(); i ++) {
				chunkData[fix[i]] = 1;
			}*/
				

			{
				float const translation4[4][4] ={
					{ 1, 0, 0, relativeChunkPos.x },
					{ 0, 1, 0, relativeChunkPos.y },
					{ 0, 0, 1, relativeChunkPos.z },
					{ 0, 0, 0, 1                  },
				};
		
				float chunkToLocal[4][4];
				
				misc::matMult(toLoc4, translation4, &chunkToLocal);
				
				if(debug) {
					glUseProgram(debugProgram);
					glUniformMatrix4fv(db_model_matrix_u, 1, GL_TRUE, &chunkToLocal[0][0]);
					glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
					glDrawArrays(GL_TRIANGLES, 0, 36);
					glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
					glUseProgram(mainProgram);
				}
				
				glUniformMatrix4fv(model_matrix_u, 1, GL_TRUE, &chunkToLocal[0][0]);
				glDrawArrays(GL_TRIANGLES, 0, 36);
			}
		}
		
		if(isFreeCam){
			auto const playerRelativePos{ vec3f((playerCoord_ - player).position()) };
			float playerToLocal[4][4];
			float const translation4[4][4] ={
					{ 1, 0, 0, playerRelativePos.x },
					{ 0, 1, 0, playerRelativePos.y },
					{ 0, 0, 1, playerRelativePos.z },
					{ 0, 0, 0, 1                   },
			};
						
			misc::matMult(toLoc4, translation4, &playerToLocal);
			
			glUseProgram(playerProgram);
			
			glUniformMatrix4fv(pl_modelMatrix_u, 1, GL_TRUE, &playerToLocal[0][0]);
			glBindVertexArray(vao);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
						
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), &verts[0]);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, (radialCount+1)*2);
			
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(topCap), &topCap[0]);
			glDrawArrays(GL_TRIANGLE_FAN, 0, radialCount+1);
			
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(botCap), &botCap[0]);
			glDrawArrays(GL_TRIANGLE_FAN, 0, radialCount+1);
			
			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0); 
		}
		
		
		if(positions.size() > 1 && debug){
			float const translation4[4][4] ={
				{ 1, 0, 0, (float)-position.x },
				{ 0, 1, 0, (float)-position.y },
				{ 0, 0, 1, (float)-position.z },
				{ 0, 0, 0, 1                  },
			};
	
			float possToLocal[4][4];
				
			misc::matMult(toLoc4, translation4, &possToLocal);
			glUseProgram(debugProgram2);
			glUniformMatrix4fv(db2_model_matrix_u, 1, GL_TRUE, &possToLocal[0][0]);
			
			glBindVertexArray(va);
			glBindBuffer(GL_ARRAY_BUFFER, vb);
			glBufferData(GL_ARRAY_BUFFER, sizeof(positions[0]) * positions.size(), &positions[0], GL_DYNAMIC_DRAW);
			glEnableVertexAttribArray(0);

			glClear(GL_DEPTH_BUFFER_BIT);
			glDepthMask(GL_FALSE);
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glDrawArrays(GL_LINE_STRIP, 0, positions.size());
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glDepthMask(GL_TRUE);
			
			glDisableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0); 
		}
		
		testInfo = false; 
		glfwSwapBuffers(window);
		glfwPollEvents();
		
		if(_i_ == 0) {
			mc.add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startFrame).count());
			//if(mc.index() == 0) std::cout << "mean:" << mc.mean() << '\n';
		}
		else _i_--;
		
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
    }

    glfwTerminate();
	
    return 0;
}
