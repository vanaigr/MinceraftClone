cmake_minimum_required(VERSION 3.30.2)

project(game)

set(NAME game)
add_executable(${NAME})

target_include_directories(${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/dependencies/include" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(${NAME} PRIVATE GLEW_STATIC GLEW_NO_GLU _CRT_SECURE_NO_WARNINGS)
target_compile_options(${NAME} PRIVATE
    -O3 -mssse3

    -pedantic

    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
    -Wno-unused-label -Wno-unused-private-field

)
target_compile_features("${NAME}" PRIVATE cxx_std_17)

file(GLOB_RECURSE GAME_SOURCES "${CMAKE_SOURCE_DIR}/src/game/*.cpp")
file(GLOB_RECURSE FONT_SOURCES "${CMAKE_SOURCE_DIR}/src/font/*.cpp")
file(GLOB_RECURSE IMAGE_SOURCES "${CMAKE_SOURCE_DIR}/src/image/*.cpp")
target_sources(${NAME} PRIVATE ${SOURCES} ${GAME_SOURCES} ${FONT_SOURCES} ${IMAGE_SOURCES})

target_link_libraries(${NAME} "${CMAKE_SOURCE_DIR}/dependencies/GLFW/glfw3.lib")
target_link_libraries(${NAME} "${CMAKE_SOURCE_DIR}/dependencies/GLEW/glew32s.lib")
target_link_libraries(${NAME} opengl32.dll gdi32.dll user32.dll kernel32.dll)
