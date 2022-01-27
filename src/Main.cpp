#include <GLEW/glew.h>
#include <GLFW/glfw3.h>

#include"Vector.h"
#include"ShaderLoader.h"

#include <iostream>
#include<chrono>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include"Read.h"
#include"Misc.h"
#include"PerlinNoise.h"

#include<vector>
#include<array>

# define PI 3.14159265358979323846

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2<uint32_t> windowSize{ 1920, 1080 };
#else
static const vec2<uint32_t> windowSize{ 1000, 600 };
#endif // FULLSCREEN

static const vec2<double> windowSize_d{ windowSize.convertedTo<double>() };

static vec2<double> mousePos(0, 0), pmousePos(0, 0);

template<typename El, size_t i_, size_t j_> 
static inline void copy2DArray(El const (&in)[i_][j_], El (&out)[i_][j_]) {
	for(size_t i = 0; i < i_; ++i) {
		for(size_t j = 0; j < j_; ++j) {
			out[i][j] = in[i][j];
		}
	}
}

struct Chunks {
private:
	std::vector<int> vacant{};
	std::vector<int> used_{};
public:
	static constexpr int const chunkDim = 16;
	using ChunkData = std::array<uint16_t, chunkDim*chunkDim*chunkDim>;
	std::vector<int> used{};
	std::vector<vec3<int32_t>> chunksPos{};
	std::vector<Chunks::ChunkData> chunksData{};
	std::vector<uint8_t> chunkNew{};
	
	//returns used[] position
	int reserve() {
		int index;
		int usedSize = used.size();

		if(!vacant.empty()) { 
			index = vacant[vacant.size()-1];
			vacant.pop_back();
		}
		else {
			index = usedSize;
			chunksPos.resize(index+1);
			chunksData.resize(index+1);
			chunkNew.resize(index+1);
		}
		used.push_back(index);
		
		return usedSize;
	}

	void recycle(int const index) {
		auto chunkIndex = used[index];
		used.erase(used.begin()+index);
		vacant.push_back(chunkIndex);
	}
	
	template<typename Action>
	void forEachUsed(Action &&action) const {
		for(auto const chunkIndex : used) {
			action(chunkIndex);
		}
	}
	
	template<typename Predicate>
	void filterUsed(Predicate&& keep) {
		auto const sz = used.size();
		
		for(size_t i = 0; i < sz; ++i) {
			auto const &chunkIndex = used[i];
			if(keep(chunkIndex)) used_.push_back(chunkIndex);
			else vacant.push_back(chunkIndex);
		}
		
		used.resize(0);
		used.swap(used_);
			
		/*for(size_t i = 0; i < sz; ++i) {
			auto const &chunkIndex = used[i];
			if(!keep(chunkIndex)) used[i]=-1;
		}
		
		int endIndex = sz-1;
		for(int i = 0; i <= endIndex; ++i) {
			if(used[i] == -1) {
                for(;endIndex > -1; --endIndex) {
                    if(used[endIndex]!=-1) {
                        if(endIndex > i)  {
							used[i] = used[endIndex];
							used[endIndex] = -1;//
							endIndex--;//for checking.   used[i] = used[endIndex--];
							
						}
                        break;
                    }	
				}					
			}
		}//produces chunk re-generation in the middle of already generated area
		
		
		
		if(true)
			for(int i = 0; i < sz; i ++) {
				if(i <= endIndex && used[i] == -1) {
					std::cout << "Bug in algorithm ==-1: i=" << i << ",endIndex=" << endIndex << ",vector:\n";
					for(int j = 0; j < sz; j ++) {
						std::cout << used[j] << ' ';
					}
					exit(-1);
				}
				else if(i > endIndex && used[i] != -1) {
					std::cout << "Bug in algorithm !=-1: i=" << i << ",endIndex=" << endIndex << ",vector:" << std::endl;
					for(int j = 0; j < sz; j ++) {
						std::cout << used[j] << ' ';
					}
					exit(-1);
				}
			}
		
		used.resize(endIndex+1);*/
	}
};

struct viewport {
    vec3<double> position{};
    vec2<double> rotation{};
	double aspectRatio;// height / width
	double fov;
	double near, far;

    constexpr vec3<double> rightDir() const {
        return vec3<double>(cos(rotation.x), 0, sin(rotation.x));
    }
    constexpr vec3<double> topDir() const {
        return vec3<double>(-sin(rotation.x) * sin(rotation.y), cos(rotation.y), cos(rotation.x) * sin(rotation.y));
    }
    constexpr vec3<double> forwardDir() const {
        return topDir().cross(rightDir());
		//return vec3<double>(cos(rotation.y) * sin(rotation.x), sin(rotation.y), -cos(rotation.y) * cos(rotation.x));
    }
	
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void localToGlobalSpace(O (*mat_out)[3][3]) const {
		auto const rd = rightDir();
		auto const td = topDir();
		auto const fd = forwardDir();
		
		O const pm[3][3] = {
			{ static_cast<O>(rd.x), static_cast<O>(td.x), static_cast<O>(fd.x) },
			{ static_cast<O>(rd.y), static_cast<O>(td.y), static_cast<O>(fd.y) },
			{ static_cast<O>(rd.z), static_cast<O>(td.z), static_cast<O>(fd.z) }
		}; //rotMatrix after scaleMatrix
		copy2DArray<O, 3, 3>(
			pm,
			*mat_out
		);
	}
	
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void globalToLocalSpace(O (*mat_out)[3][3]) const {
		auto const rd = rightDir();
		auto const td = topDir();
		auto const fd = forwardDir();
		
		O const pm[3][3] = {
			{ static_cast<O>(rd.x), static_cast<O>(rd.y), static_cast<O>(rd.z) },
			{ static_cast<O>(td.x), static_cast<O>(td.y), static_cast<O>(td.z) },
			{ static_cast<O>(fd.x), static_cast<O>(fd.y), static_cast<O>(fd.z) }
		};
		copy2DArray<O, 3, 3>(
			pm,
			*mat_out
		);
	}
	
	/*
		Mat4x4 projMat = new Mat4x4();
		projMat.m[0][0] = aspect * fovRad;
		projMat.m[1][1] = fovRad;
		projMat.m[2][2] = far / (far - near);
		projMat.m[3][2] = (-far * near) / (far - near);
		projMat.m[2][3] = 1f;
		//projMat.m[3][3] = 0;
  
		{ aspect * fovRad, 0, 0             , 0 } 
		{ 0,          fovRad, 0             , 0 }
		{ 0, 0         , far / (far - near), (-far * near) / (far - near) }
		{ 0, 0,                           1, 0 }
  
		far * (z - near) / (far-near)
  
		far * (z - near) / (far-near)
  
  
		map = lerp(0, far-near, unlerp(near, far, z));
  
		return (z - near) / (far - near) * far;
	*/
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void projectionMatrix(O (*mat_out)[4][4]) const {
		O const pm[4][4] = {
			{ static_cast<O>(aspectRatio*fov), static_cast<O>(0.0), static_cast<O>(0), static_cast<O>(0.0) },
			{ static_cast<O>(0.0), static_cast<O>(fov), static_cast<O>(0.0), static_cast<O>(0.0) },
			{ static_cast<O>(0.0), static_cast<O>(0.0), static_cast<O>(1.0/(far-near)), static_cast<O>(-(far*near)/(far-near)) }, //z: [near, far] -> [0, 1]
			{ static_cast<O>(0.0), static_cast<O>(0.0), static_cast<O>(1.0), static_cast<O>(0.0) }
		};
		copy2DArray<O, 4, 4>(
			pm,
			*mat_out
		);
	}
	
};

static bool isPan{ false };
static bool isZoomMovement{ false };
static double zoomMovement{ 0 };
static double size = 1;

static bool shift{ false }, ctrl{ false };

static double const maxMovementSpeed = 1.0 / 7.5;
static double currentMovementSpeed = 0;
static vec3<double> movementDir{};
static double speedModifier = 2.5;


static size_t const viewportCount = 1;
static viewport viewports[viewportCount]{ 
	viewport{
		vec3<double>{ 0, 20, 0 },
		vec2<double>{ misc::pi / 2.0, 0 },
		windowSize_d.y / windowSize_d.x,
		90.0 / 180.0 * misc::pi,
		0.001,
		1000
	} 
};

static viewport &mainViewport{ viewports[0] };
static size_t viewport_index{ 0 };

static viewport& viewport_current() {
    return viewports[viewport_index];
}

static void next_viewport() {
    viewport_index = (viewport_index + 1) % viewportCount;
}

static void reloadShaders();

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    bool isPress = !(action == GLFW_RELEASE);
    if (key == GLFW_KEY_W)
        movementDir.z = 1 * isPress;
    else if(key == GLFW_KEY_S)
        movementDir.z = -1 * isPress;
	
	if (key == GLFW_KEY_Q)
        movementDir.y = 1 * isPress;
    else if(key == GLFW_KEY_E)
        movementDir.y = -1 * isPress;

    if(key == GLFW_KEY_D)
        movementDir.x = 1 * isPress;
    else if(key == GLFW_KEY_A)
        movementDir.x = -1 * isPress;
    if(key == GLFW_KEY_TAB) {
        if(action == GLFW_PRESS)
            next_viewport();
    }
	if(key == GLFW_KEY_F5 && action == GLFW_PRESS) {
		reloadShaders();
	}

    shift = (mods & GLFW_MOD_SHIFT) != 0;
    ctrl = (mods & GLFW_MOD_CONTROL) != 0;
}

static void cursor_position_callback(GLFWwindow* window, double mousex, double mousey) {
    static vec2<double> relativeTo{ 0, 0 };
    vec2<double> mousePos_{ mousex,  mousey };
    
    relativeTo += mousePos_;
    mousePos_.x = misc::modf(mousePos_.x, windowSize_d.x);
    mousePos_.y = misc::modf(mousePos_.y, windowSize_d.y);
    relativeTo -= mousePos_;

    glfwSetCursorPos(window, mousePos_.x, mousePos_.y);

    mousePos = vec2<double>(relativeTo.x + mousePos_.x, -relativeTo.y + windowSize_d.y - mousePos_.y);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if(button == GLFW_MOUSE_BUTTON_LEFT)
        if (action == GLFW_PRESS) {
            isPan = true;
        }
        else if (action == GLFW_RELEASE) {
            isPan = false;
        }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        if (action == GLFW_PRESS) {
            zoomMovement = 0;
            isZoomMovement = true;
        }
        else if (action == GLFW_RELEASE) {
            isZoomMovement = false;
            zoomMovement = 0;
        }
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    size -= size * yoffset * 0.07;
}

static void update() {
    auto& currentViewport = viewport_current();

    vec2<double> diff = (mousePos - pmousePos) / windowSize_d;
    if (isZoomMovement) {
        zoomMovement += diff.x;

        constexpr double movementFac = 0.05;

        const auto forWardDir = currentViewport.forwardDir();
        currentViewport.position += forWardDir * zoomMovement * movementFac * size;
    }
    if (isPan) {
        currentViewport.rotation += diff * (2 * misc::pi);
        currentViewport.rotation.y = misc::clamp(currentViewport.rotation.y, -misc::pi / 2 + 0.001, misc::pi / 2 - 0.001);
    }
    if (movementDir.lengthSuqare() != 0) {
		double projection[3][3];
		currentViewport.localToGlobalSpace(&projection);
		
        vec3<double> movement = vecMult(projection, movementDir);
		//vec3<double> movement = vec3<double>{ movementDir.x, movementDir.y, 0.0 };
        currentViewport.position += movement.normalized() * currentMovementSpeed
            * (shift ? 1.0*speedModifier : 1)
            * (ctrl  ? 1.0/speedModifier : 1);

        currentMovementSpeed = misc::lerp(currentMovementSpeed, maxMovementSpeed, 0.2);
    }

    pmousePos = mousePos;
}

//static GLuint position_u;
static GLuint rightDir_u;
static GLuint topDir_u;
static GLuint time_u;
static GLuint mouseX_u;
static GLuint chunkUBO;
static GLuint projection_u;
static GLuint model_matrix_u, chunkNew_u;

static GLuint chunkIndex_b;

static GLuint bgProjection_u;
static GLuint bgRightDir_u;
static GLuint bgTopDir_u;

static GLuint mainProgram = 0;
static GLuint bgProgram = 0;

//uint32_t chunk[16 * 16 * 16/2];

static void reloadShaders() {
	glDeleteProgram(mainProgram);
	mainProgram = glCreateProgram();
    ShaderLoader sl{};
    //sl.addIdentityVertexShader("id");//.addScreenSizeTriangleStripVertexShader("vert");
	
	//https://gist.github.com/rikusalminen/9393151
	//... usage: glDrawArrays(GL_TRIANGLES, 0, 36), disable all vertex arrays
	sl.addShaderFromCode(
		R"(#version 420

		uniform mat4 projection;
		uniform mat4 model_matrix;
		//uniform vec3 translation;
		
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


    rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
    topDir_u = glGetUniformLocation(mainProgram, "topDir");
	projection_u = glGetUniformLocation(mainProgram, "projection");
	model_matrix_u = glGetUniformLocation(mainProgram, "model_matrix");
	
	//images
    GLuint textures[1];
    char const* (paths)[1]{ "assets/atlas.bmp"
    };
    loadGLImages<1>(textures, paths);

    GLuint const atlasTex_u = glGetUniformLocation(mainProgram, "atlas");
    chunkNew_u = glGetUniformLocation(mainProgram, "chunkNew");

    glUniform1i(atlasTex_u, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);

    /*GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(field), field, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);*/
	
	chunkIndex_b = glGetUniformBlockIndex(mainProgram, "Chunk");
	glGenBuffers(1, &chunkUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, chunkUBO);
	glUniformBlockBinding(mainProgram, chunkIndex_b, 1);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, chunkUBO);
	glBufferData(GL_UNIFORM_BUFFER, 8192+12, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	
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
	
	
	float projection[4][4];
	viewport_current().projectionMatrix(&projection);
	
	glUseProgram(bgProgram);
	glUniformMatrix4fv(bgProjection_u, 1, GL_TRUE, &projection[0][0]);
	
	glUseProgram(mainProgram);
	glUniformMatrix4fv(projection_u, 1, GL_TRUE, &projection[0][0]);
	
}

template<typename T, typename L>
inline void apply(size_t size, T &t, L&& l) {
	for(size_t i = 0; i < size; ++i) 
		if(l(t[i], i) == true) break;
}

double sign(double const val) {
    return (0.0 < val) - (val < 0.0);
}

Chunks chunks{};

siv::PerlinNoise perlin{ (uint32_t)rand() };
static int viewDistance = 5;

void generateChunk(vec3<int> const pos, Chunks::ChunkData &data) {
	for(int y = 0; y < Chunks::chunkDim; ++y) 
		for(int z = 0; z < Chunks::chunkDim; ++z)
			for(int x = 0; x < Chunks::chunkDim; ++x) {
				auto const value = perlin.octave2D(
					(pos.x * Chunks::chunkDim + x) / 20.0, 
					(pos.z * Chunks::chunkDim  + z) / 20.0, 
					3);
				auto const height = misc::map<double>(value, -1, 1, 5, 15);
				if(height > (pos.y * Chunks::chunkDim + y))
					data[x+y*Chunks::chunkDim+z*Chunks::chunkDim*Chunks::chunkDim] = 1;
				else 
					data[x+y*Chunks::chunkDim+z*Chunks::chunkDim*Chunks::chunkDim] = 0;
			}
	
}

void loadChunks() {
	static std::vector<int8_t> chunksPresent{};
	auto const viewWidth = (viewDistance*2+1);
	chunksPresent.resize(viewWidth*viewWidth*viewWidth);
	for(int i = 0; i< chunksPresent.size(); i++)
		chunksPresent[i] = -1;
	
	auto const currentViewport{ viewport_current() };
	vec3<int32_t> playerChunk{ static_cast<vec3<int32_t>>((currentViewport.position / Chunks::chunkDim).appliedFunc<double(*)(double)>(floor)) };
	
	vec3<int32_t> b{viewDistance, viewDistance, viewDistance};
	
	chunks.filterUsed([&](int chunkIndex) -> bool { 
		auto const relativeChunkPos = chunks.chunksPos[chunkIndex] - playerChunk;
		if(chunks.chunkNew[chunkIndex]>0) chunks.chunkNew[chunkIndex]--;
		if(relativeChunkPos.in(-b, b)) {
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
				
					int const chunkIndex{ chunks.used[chunks.reserve()] };
					
					chunks.chunksPos[chunkIndex] = chunkPos;
					generateChunk(chunkPos, chunks.chunksData[chunkIndex]);
					chunks.chunkNew[chunkIndex] = 30;
				}
			}
		}
	}
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

    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
        glfwTerminate();
        return -1;
    }
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
    fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));

    //callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);


	//load shaders
	reloadShaders();
	
	loadChunks();
	
	while ((err = glGetError()) != GL_NO_ERROR)
    {
        std::cout << err << std::endl;
    }

    auto start = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window))
    {
		auto &currentViewport = viewport_current();
        vec3<double> const pos{ currentViewport.position };
        auto const rightDir{ currentViewport.rightDir() };
        auto const topDir{ currentViewport.topDir() };
		
		//glClear(GL_COLOR_BUFFER_BIT);
		
		glUseProgram(bgProgram);
		glUniform3f(bgRightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(bgTopDir_u, topDir.x, topDir.y, topDir.z);
		
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
		
		
		glUseProgram(mainProgram);
        glUniform3f(rightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(topDir_u, topDir.x, topDir.y, topDir.z);

        glUniform1f(mouseX_u, mousePos.x / windowSize_d.x);

        glUniform1f(time_u,
            sin(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()
            )
        );
		
		
		float toLoc[3][3];
		float toGlob[3][3];
		currentViewport.localToGlobalSpace(&toGlob);
		currentViewport.globalToLocalSpace(&toLoc);
		vec3<int32_t> const playerChunk{ static_cast<vec3<int32_t>>((pos / Chunks::chunkDim).appliedFunc<double(*)(double)>(floor)) };
		auto const playerPosInChunk{ 
			pos.applied([](auto const it, auto ignore) -> auto { return misc::modd(it, 16); }) 
		};
		
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
		chunksSorted.reserve(chunks.used.size());
		for(auto const chunkIndex : chunks.used) {
			auto const chunkPos{ chunks.chunksPos[chunkIndex] };
			vec3<float> const relativeChunkPos{ 
				static_cast<decltype(playerPosInChunk)>(chunkPos-playerChunk)*Chunks::chunkDim
				+(Chunks::chunkDim/2.0f)
				-playerPosInChunk
			};
			chunksSorted.push_back({ relativeChunkPos.lengthSuqare(), chunkIndex });
		}
		std::sort(chunksSorted.begin(), chunksSorted.end(), [](ChunkSort const c1, ChunkSort const c2) -> bool {
			return c1.sqDistance > c2.sqDistance; //chunks located nearer are rendered last
		});
		
		for(auto const [_ignore_, chunkIndex] : chunksSorted) {
			auto const chunkPos{ chunks.chunksPos[chunkIndex] };
			auto const &chunkData{ chunks.chunksData[chunkIndex] };
			vec3<float> const relativeChunkPos{ 
				static_cast<decltype(playerPosInChunk)>(chunkPos-playerChunk)*Chunks::chunkDim
				-playerPosInChunk
			};
			
			glBindBuffer(GL_UNIFORM_BUFFER, chunkUBO); 
				
			static_assert(sizeof relativeChunkPos == 12, "");
			glBufferSubData(GL_UNIFORM_BUFFER, 0, 12, &relativeChunkPos);
			static_assert(sizeof chunkData == 8192, "");
			glBufferSubData(GL_UNIFORM_BUFFER, 12, 8192, &chunkData);
				
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
			
			float const translation4[4][4] ={
				{ 1, 0, 0, relativeChunkPos.x },
				{ 0, 1, 0, relativeChunkPos.y },
				{ 0, 0, 1, relativeChunkPos.z },
				{ 0, 0, 0, 1                  },
			};
	
			float modelMatrix[4][4];
			misc::matMult(toLoc4, translation4, &modelMatrix);
			
			//for(int i = 0; i < 4; (std::cout << '\n'), i++) 
			//	for(int j = 0; j < 4; j ++) std::cout << modelMatrix[i][j] << ' ';
			glUniform1i(chunkNew_u, chunks.chunkNew[chunkIndex]!=0);
			glUniformMatrix4fv(model_matrix_u, 1, GL_TRUE, &modelMatrix[0][0]);
			glDrawArrays(GL_TRIANGLES, 0, 36);
		}
		
		glfwSwapBuffers(window);
		glfwPollEvents();
		
		loadChunks();

		
        while ((err = glGetError()) != GL_NO_ERROR)
            std::cout << "glError 2: " << err << std::endl;

        update();
    }

    glfwTerminate();
	
	//char dummy;
	std::cin.ignore();
    return 0;
}