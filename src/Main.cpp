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

//#define FULLSCREEN

#ifdef FULLSCREEN
static const vec2<uint32_t> windowSize{ 1920, 1080 };
#else
static const vec2<uint32_t> windowSize{ 800, 800 };
#endif // FULLSCREEN

static const vec2<double> windowSize_d{ windowSize.convertedTo<double>() };

static vec2<double> mousePos(0, 0), pmousePos(0, 0);

struct viewport {
    vec3<double> position{};
    vec2<double> rotation{};

    constexpr vec3<double> rightDir() const {
        return vec3<double>(cos(rotation.x), 0, sin(rotation.x));
    }
    constexpr vec3<double> topDir() const {
        return vec3<double>(-sin(rotation.x) * sin(rotation.y), cos(rotation.y), cos(rotation.x) * sin(rotation.y));
    }
    constexpr vec3<double> forwardDir() const {
        return topDir().cross(rightDir());
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
static double speedModifier = 1.75;


static size_t const viewportCount = 1;
static viewport viewports[viewportCount]{};

static viewport &mainViewport{ viewports[0] };
static size_t viewport_index{ 0 };

static viewport* viewport_current() {
    return &viewports[viewport_index];
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
    auto& currentViewport = *viewport_current();

    vec2<double> diff = (mousePos - pmousePos) / windowSize_d;
    if (isZoomMovement) {
        zoomMovement += diff.x;

        constexpr double movementFac = 0.05;

        const auto forWardDir = currentViewport.forwardDir();
        currentViewport.position += forWardDir * zoomMovement * movementFac * size;
    }
    if (isPan) {
        currentViewport.rotation += diff * (2 * 3.14);
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

static GLuint position_u;
static GLuint rightDir_u;
static GLuint topDir_u;
static GLuint time_u;
static GLuint mouseX_u;
static GLuint chunkUBO;

static GLuint mainProgram = 0;

uint32_t chunk[16 * 16 * 16/2];

static void reloadShaders() {
	glDeleteProgram(mainProgram);
	mainProgram = glCreateProgram();
    ShaderLoader sl{};
    sl.addScreenSizeTriangleStripVertexShader("vert");
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


    position_u = glGetUniformLocation(mainProgram, "position");
    rightDir_u = glGetUniformLocation(mainProgram, "rightDir");
    topDir_u = glGetUniformLocation(mainProgram, "topDir");
	
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
				bool is = (z > 14);//(rand() % 16) < (16 - z);
				unsigned index_ = (x + y*16 + z*16*16);
				unsigned index = index_ / 2;
				unsigned shift = (index_ % 2) * 16;
				chunk[index] |= uint32_t(is) << shift;
			}
			
	
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
        auto& currentViewport = *viewport_current();
        auto& pos = currentViewport.position;
        const auto rightDir{ currentViewport.rightDir() };
        const auto topDir{ currentViewport.topDir() };
        glUniform3f(position_u, 0, 0, 0);//currentViewport.position.x, currentViewport.position.y, currentViewport.position.z);
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

        //glClear(GL_COLOR_BUFFER_BIT);

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);

        glfwSwapBuffers(window);

        glfwPollEvents();

        while ((err = glGetError()) != GL_NO_ERROR)
        {
            std::cout << err << std::endl;
        }

        update();
    }

    glfwTerminate();
	
	char dummy;
	std::cin >> dummy;
    return 0;
}