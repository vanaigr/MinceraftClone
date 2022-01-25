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

#include<vector>

# define PI 3.14159265358979323846

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2<uint32_t> windowSize{ 1920, 1080 };
#else
static const vec2<uint32_t> windowSize{ 600, 600 };
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

template<typename C>
struct Triangle {
	vec3<C> verts_[3];
	
	Triangle(vec3<C> vert1, vec3<C> vert2, vec3<C> vert3) : verts_{ vert1, vert2, vert3 } {}
	
	Triangle(vec3<C> vert1, vec3<C> col1, vec3<C> vert2, vec3<C> col2, vec3<C> vert3, vec3<C> col3) 
		: verts_{ vert1, vert2, vert3 }
		{}
		
	vec3<C> const &verts(int i) const {
		return verts_[i];
	}
	vec3<C> &verts(int i) {
		return verts_[i];
	}
	
	template<typename Action>
	void apply(Action&& a) {
		a(verts(0), 0);
		a(verts(1), 1);
		a(verts(2), 2);
	}
	
	template<typename Action>
	Triangle<C> applied(Action&& a) const {
		return Triangle<C>{
			a(verts(0), 0),
			a(verts(1), 1),
			a(verts(2), 2)
		};
	}
	
	void intersectPlane(vec3<C> const orig, vec3<C> const n, std::vector<Triangle> &fls) const {
		struct vertInfo {
			vec3<C> to;
			vec3<C> to_norm;
			C cos;
			C dist;//signed distance
			
			vertInfo(vec3<C> const vert, vec3<C> const orig, vec3<C> const n) : 
			to{ vert - orig }, to_norm{ to.normalized() },
			cos( n.dot(to_norm) ), dist( n.dot(to) )
			{}
		};
		
		vertInfo const info[3] = {
			{ verts(0), orig, n },
			{ verts(1), orig, n },
			{ verts(2), orig, n }
		};
		
		int vertIndices[3];
		int bads = 0, goods = 3;
		for(int i = 0; i < 3; i ++) {
			if(info[i].cos < 0) vertIndices[bads++] = i;
			else vertIndices[--goods] = i;
		}
			
		if(bads == 0) {
			fls.push_back(*this);
		}
		else if(bads == 1) {
			auto const bad = vertIndices[0];
			
			auto const good1 = vertIndices[2-0];
			auto const good2 = vertIndices[2-1];
			
			vec3<C> newVerts[2];
			
			for(int i = 0; i < 2; ++i) {
				auto const good = vertIndices[2-i];
				auto const t = misc::unlerp(info[bad].dist, info[good].dist, C(0));
				newVerts[i] = misc::vec3lerp(verts(bad), verts(good), t);
			}
			
			fls.push_back(
				Triangle{ 
					verts(good1),
					vec3<float>(1, 0, 0),
					verts(good2),
					vec3<float>(1, 0, 0),
					newVerts[0],
					vec3<float>(1, 0, 0)
				}
			);
			fls.push_back(
				Triangle{ 
					newVerts[0],
					vec3<float>(1, 0, 0),
					verts(good2),
					vec3<float>(1, 0, 0),
					newVerts[1],
					vec3<float>(1, 0, 0)
				}
			);
		}
		else if(bads == 2) {
			vec3<float> color{
				0, 1, 0
			};
			auto const good = vertIndices[2];
			
			vec3<C> newVerts[2];
			
			for(int i = 0; i < 2; ++i) {
				auto const bad = vertIndices[i];
				auto const t = misc::unlerp(info[bad].dist, info[good].dist, C(0));
				newVerts[i] = misc::vec3lerp(verts(bad), verts(good), t);
			}
			
			fls.push_back(
				Triangle{ 
					verts(good),
					vec3<float>(0, 1, 0),
					newVerts[0],
					vec3<float>(0, 1, 0),
					newVerts[1],
					vec3<float>(0, 1, 0)
				}
			);
		}
		else //if(bads == 3) 
			return;
	}
	
	friend std::ostream &operator<<(std::ostream &o, Triangle const &it) {
		o << '[' << it.verts(0) << ',' << '\n'
		  << ' ' << it.verts(1) << ',' << '\n'
		  << ' ' << it.verts(2) << ']';
		return o;
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

static double const maxMovementSpeed = 1.0 / 30.0;
static double currentMovementSpeed = 0;
static vec2<double> movementDir{};
static double speedModifier = 2.5;


static size_t const viewportCount = 1;
static viewport viewports[viewportCount]{ 
	viewport{
		vec3<double>{ 0, 0, 0 },
		vec2<double>{ 0, 0 },
		windowSize_d.y / windowSize_d.x,
		90.0 / 180.0 * misc::pi,
		0.001,
		100
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
        movementDir.y = 1 * isPress;
    else if(key == GLFW_KEY_S)
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
        vec3<double> movement = currentViewport.rightDir() * movementDir.x + currentViewport.forwardDir() * movementDir.y;
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

static GLuint mainProgram = 0;

uint32_t chunk[16 * 16 * 16/2];

static void reloadShaders() {
	glDeleteProgram(mainProgram);
	mainProgram = glCreateProgram();
    ShaderLoader sl{};
    //sl.addIdentityVertexShader("id");//.addScreenSizeTriangleStripVertexShader("vert");
    sl.addShaderFromCode(R"(
		#version 330
		layout(location = 0) in vec3 in_position;
		//layout(location = 1) in vec3 in_col;
		uniform mat4 projection;
		void main() {
			gl_Position = projection * vec4(in_position, 1.0);// + vec4(in_col * 0.00001, 0);
		})"
		,
		GL_VERTEX_SHADER,
		"name"
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


    //position_u = glGetUniformLocation(mainProgram, "position");
    rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
    topDir_u = glGetUniformLocation(mainProgram, "topDir");
	projection_u = glGetUniformLocation(mainProgram, "projection");
	
	//images
    GLuint textures[1];
    char const* (paths)[1]{ "assets/atlas.bmp"
    };
    loadGLImages<1>(textures, paths);

    GLuint const atlasTex_u = glGetUniformLocation(mainProgram, "atlas");

    glUniform1i(atlasTex_u, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);

    /*GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(field), field, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);*/
	
	GLuint chunkIndex = glGetUniformBlockIndex(mainProgram, "Chunk");
	glGenBuffers(1, &chunkUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, chunkUBO); 
	glUniformBlockBinding(mainProgram, chunkIndex, 1);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, chunkUBO);
	assert(sizeof chunk == 8192);
	glBufferData(GL_UNIFORM_BUFFER, 8192+12, NULL, GL_DYNAMIC_DRAW);
	glBufferSubData(GL_UNIFORM_BUFFER, 12, 8192, &chunk);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/*void invertMatrix3To(double const (&m)[3][3], double (*minv)[3][3]) {//https://stackoverflow.com/a/18504573/15291447
double const det = 
			m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
            m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
            m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
			
			std::cout << det << '\n';

double const invdet = 1.0 / det;

(*minv)[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) / invdet;
(*minv)[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / invdet;
(*minv)[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / invdet;
(*minv)[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / invdet;
(*minv)[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / invdet;
(*minv)[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) / invdet;
(*minv)[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) / invdet;
(*minv)[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) / invdet;
(*minv)[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) / invdet;
}*/

template<typename El, size_t r1, size_t c1r2, size_t c2>
void matMult(El const (&m1)[r1][c1r2], El const (&m2)[c1r2][c2], El (*o)[r1][c2]) {
	for(size_t c = 0; c < c2; c++) {
		for(size_t r = 0; r < r1; r++) {
			El sum(0);
			for(size_t cr = 0; cr < c1r2; cr++)
				sum += m1[r][cr] * m2[cr][c];
			(*o)[r][c] = sum;
		}
	}
}
template<typename El>
vec3<El> vecMult(El const (&m1)[3][3], vec3<El> const &m2) {
	static_assert(sizeof(m2) == sizeof(float[3][1]), "");
	vec3<El> o;
	matMult<El, 3, 3>(m1, *(float(*)[3][1])(void*)&m2, (float(*)[3][1])(void*)&o);
	return o;
}

template<typename T, typename L>
inline void apply(size_t size, T &t, L&& l) {
	for(size_t i = 0; i < size; ++i) 
		if(l(t[i], i) == true) break;
}

double sign(double const val) {
    return (0.0 < val) - (val < 0.0);
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

    window = glfwCreateWindow(windowSize.x, windowSize.y, "VMC", NULL, NULL);

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
	
    fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));

    //callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);

	for(int i = 0; i < (16*16*16/2); i ++) 
		chunk[i] = 0;
	
	for (unsigned z = 0; z < 16; ++z)
		for (unsigned y = 0; y < 16; ++y)
			for (unsigned x = 0; x < 16; ++x) {
				bool is = (rand() % 16) < (16 - y);
				unsigned index_ = (x + y*16 + z*16*16);
				unsigned index = index_ / 2;
				unsigned shift = (index_ % 2) * 16;
				chunk[index] |= uint32_t(is) << shift;
			}
			
  float near = 0.1;
  float far = 100.0;
  float fov = 90;
  float aspect = (float) windowSize_d.y / (float) windowSize_d.x;
  float fovRad = 1.0 / tan(fov / 360.0 * PI); 
	
Triangle<float> chunk_1[] = {
    Triangle<float>{
		vec3<float>{-1.0f,-1.0f,-1.0f},
		vec3<float>{-1.0f,-1.0f, 1.0f},
		vec3<float>{-1.0f, 1.0f, 1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f,-1.0f}, 
    	vec3<float>{-1.0f,-1.0f,-1.0f},
    	vec3<float>{-1.0f, 1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f,-1.0f, 1.0f},
    	vec3<float>{-1.0f,-1.0f,-1.0f},
    	vec3<float>{1.0f,-1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f,-1.0f},
    	vec3<float>{1.0f,-1.0f,-1.0f},
    	vec3<float>{-1.0f,-1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{-1.0f,-1.0f,-1.0f},
    	vec3<float>{-1.0f, 1.0f, 1.0f},
    	vec3<float>{-1.0f, 1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f,-1.0f, 1.0f},
    	vec3<float>{-1.0f,-1.0f, 1.0f},
    	vec3<float>{-1.0f,-1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{-1.0f, 1.0f, 1.0f},
    	vec3<float>{-1.0f,-1.0f, 1.0f},
    	vec3<float>{1.0f,-1.0f, 1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f, 1.0f},
    	vec3<float>{1.0f,-1.0f,-1.0f},
    	vec3<float>{1.0f, 1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f,-1.0f,-1.0f},
    	vec3<float>{1.0f, 1.0f, 1.0f},
    	vec3<float>{1.0f,-1.0f, 1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f, 1.0f},
    	vec3<float>{1.0f, 1.0f,-1.0f},
    	vec3<float>{-1.0f, 1.0f,-1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f, 1.0f},
    	vec3<float>{-1.0f, 1.0f,-1.0f},
    	vec3<float>{-1.0f, 1.0f, 1.0f}
	},
    Triangle<float>{
		vec3<float>{1.0f, 1.0f, 1.0f},
    	vec3<float>{-1.0f, 1.0f, 1.0f},
    	vec3<float>{1.0f,-1.0f, 1.0f}
	}
};
	/*static const vec3<double> chunk[8] {
		 vec3<double>(0, 0, 0)
		,vec3<double>(0, 0, 1)
		,vec3<double>(0, 1, 0)
		,vec3<double>(0, 1, 1)
		,vec3<double>(1, 0, 0)
		,vec3<double>(1, 0, 1)
		,vec3<double>(1, 1, 0)
		,vec3<double>(1, 1, 1)
	};*/

	//rand();
	//rand();
	//rand();
	//rand();
	//rand();
	//rand();
	
	/*//test
	auto const t = Triangle<double> {
		vec3<double>((rand()-16000.0) / 20000.0, (rand()-16000.0) / 20000.0, (rand()-20000.0) / 20000.0),
		vec3<double>((rand()-16000.0) / 20000.0, (rand()-16000.0) / 20000.0, rand() / 20000.0),
		vec3<double>((rand()-16000.0) / 20000.0, (rand()-16000.0) / 20000.0, rand() / 20000.0)
	};
	
	std::vector<Triangle<double>> tris{};
	t.intersectPlane(
		vec3<double>(0.0, 0.0, 0.0),
		vec3<double>(0.0, 0.0, 1.0),
		tris
	);
	
	std::cout << t << "\n\n";
	
	for(int i = 0; i < tris.size(); i ++) 
		std::cout << tris[i] << ",\n";
	
	exit(0);*/
	
	apply(12, chunk_1, [](Triangle<float> &it, size_t i) -> bool {
		it.apply(
			[](vec3<float> &vert, int i_) {
				vert = vert.applied([](float const el, int i__) -> double { return (el + 1) / 2 * 16; });
			}
		);
		return false;
	});
	

	GLuint vbo;
	glGenBuffers(1, &vbo);
	
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0); 

	//load shaders
	reloadShaders();
	
	while ((err = glGetError()) != GL_NO_ERROR)
    {
        std::cout << err << std::endl;
    }

    auto start = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window))
    {
        //sending
        auto& currentViewport = viewport_current();
        auto& pos = currentViewport.position;
        const auto rightDir{ currentViewport.rightDir() };
        const auto topDir{ currentViewport.topDir() };
        //glUniform3f(position_u, currentViewport.position.x, currentViewport.position.y, currentViewport.position.z);
        glUniform3f(rightDir_u, rightDir.x, rightDir.y, rightDir.z);
        glUniform3f(topDir_u, topDir.x, topDir.y, topDir.z);
		
		float arr[3] = { (float)-pos.x, (float)-pos.y, (float)-pos.z };
		glBindBuffer(GL_UNIFORM_BUFFER, chunkUBO); 
		static_assert(sizeof arr == 12, "");
		glBufferSubData(GL_UNIFORM_BUFFER, 0, 12, &arr);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glUniform1f(mouseX_u, mousePos.x / windowSize_d.x);

        glUniform1f(time_u,
            sin(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()
            )
        );

        glClear(GL_COLOR_BUFFER_BIT);
		
		//GLfloat verts_proj[36*3];
	
		float toLoc[3][3];
		float toGlob[3][3];
		viewport_current().localToGlobalSpace(&toGlob);
		viewport_current().globalToLocalSpace(&toLoc);
		
		int const constexpr trisSize = 2;
		struct Tris {
			std::vector<Triangle<float>> (triss[trisSize]){};
			int trissIndex = 0;

			std::vector<Triangle<float>> &prev() {
				return triss[(trissIndex+trisSize-1)%trisSize];
			}
			std::vector<Triangle<float>> &cur() {
				return triss[trissIndex];
			}
			std::vector<Triangle<float>> &next() {
				return triss[(trissIndex+1)%trisSize];
			}
			
			Tris &operator++() {
				cur().clear();
				trissIndex = (trissIndex+1) % trisSize;
				return *this;
			}
		};
		
		Tris tris{};
		tris.cur().reserve(12);
		
		for(int i = 0; i < 12; i ++) 
			tris.cur().push_back(chunk_1[i].applied([&](vec3<float> const &vert, int const i) -> vec3<float> { return vert - pos; }));
		
		//culling
		if(false){//near
			vec3<float> const orig{ vecMult(toGlob, vec3<float>{ 0, 0, 0.01 }) };
			vec3<float> const normal{ vecMult(toGlob, vec3<float>{ 0, 0, 1 }) };
			
			for(int i = 0; i < tris.cur().size(); i++) {
				tris.cur()[i].intersectPlane(
					orig,
					normal,
					tris.next()
				);
			}
			++tris;
		}
		
		if(false){//sides
			float ar = (windowSize_d.x / windowSize_d.y);
			vec3<float> planesRay[4]={
				vec3<float>{  ar,  0, 0 },
				vec3<float>{ -ar,  0, 0 },
				vec3<float>{  0,  1, 0 },
				vec3<float>{  0, -1, 0 }
			};
			
			auto const forward{ vecMult<float>(toGlob, currentViewport.forwardDir()) };
			for(int i = 0; i < 4; i ++) {	
				vec3<float> const orig{ vecMult(toGlob, planesRay[i]).normalized() };
	
				vec3<float> const rayAlongPlane2{ orig.cross(forward) };
				vec3<float> normal{ rayAlongPlane2.cross(orig) };
				
				auto &cur = tris.cur();
				auto &next = tris.next();
				for(int i = 0; i < cur.size(); i++) {
					cur[i].intersectPlane(
						orig,
						normal,
						next
					);
				}
				++tris;
			}
		}
		
		//projection
		vec3<float> verts[3];
		int index = 0, chIndex = 0;
		apply(12, tris.cur(), [&](Triangle<float>const &it, size_t const i) -> bool {
			tris.next().push_back(
				it.applied([&](vec3<float> vert, size_t const i_) -> vec3<float> {
					return vecMult(toLoc, vert);
				})
			);
			return false;
		});
		++tris;
		
		auto &cur = tris.cur();
		
		
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		
		float projection[4][4];
		currentViewport.projectionMatrix(&projection);
		glUniformMatrix4fv(projection_u, 1, GL_TRUE, &projection[0][0]);
		
		glBufferData(
			GL_ARRAY_BUFFER, cur.size()*sizeof(float)*3*3,
			&cur[0], GL_DYNAMIC_DRAW
		);


		glBindVertexArray(vao);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
		glEnableVertexAttribArray(0);

		glDrawArrays(GL_TRIANGLES, 0, cur.size() * 3);


		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
   
		glfwSwapBuffers(window);
		glfwPollEvents();

        //glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);


        //glfwSwapBuffers(window);

        //glfwPollEvents();
		
        while ((err = glGetError()) != GL_NO_ERROR)
        {
            std::cout << "glError: " << err << std::endl;
        }

        update();
    }

    glfwTerminate();
	
	//char dummy;
	std::cin.ignore();
    return 0;
}