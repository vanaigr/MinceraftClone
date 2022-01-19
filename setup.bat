@echo off

if not exist dependencies mkdir dependencies
if not exist dependencies\GLFW mkdir dependencies\GLFW
if not exist dependencies\GLEW mkdir dependencies\GLEW
if not exist dependencies\include mkdir dependencies\include
if not exist dependencies\include\GLFW mkdir dependencies\include\GLFW
if not exist dependencies\include\GLEW mkdir dependencies\include\GLEW

echo setting up glfw
if not exist glfw-3.3.2.bin.WIN64.zip curl -sSL -o glfw-3.3.2.bin.WIN64.zip https://github.com/glfw/glfw/releases/download/3.3.2/glfw-3.3.2.bin.WIN64.zip
tar -xf glfw-3.3.2.bin.WIN64.zip
move glfw-3.3.2.bin.WIN64\lib-vc2019\glfw3.lib dependencies\GLFW
move glfw-3.3.2.bin.WIN64\include\GLFW\glfw3.h dependencies\include\GLFW
del glfw-3.3.2.bin.WIN64.zip
rmdir /s /q glfw-3.3.2.bin.WIN64

echo settng up glew
if not exist glew-2.1.0-win32.zip curl -sSL -o glew-2.1.0-win32.zip https://sourceforge.net/projects/glew/files/glew/2.1.0/glew-2.1.0-win32.zip/download
tar -xf glew-2.1.0-win32.zip
move glew-2.1.0\lib\Release\x64\glew32s.lib dependencies\GLEW
move glew-2.1.0\include\GL\glew.h dependencies\include\GLEW
del glew-2.1.0-win32.zip
rmdir /s /q glew-2.1.0

pause