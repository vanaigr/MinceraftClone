INCLUDES=-I .\dependencies\include
SOURCE_DIR=src
SHADERS_DIR=shaders
OBJECT_DIR=obj
CPP_EXCLUDES=
WS= -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-private-field
CFLAGS= -O3 -std=c++17 -pedantic -c $(WS) -mssse3
PPFLAGS=-D GLEW_STATIC -D GLEW_NO_GLU -D _CRT_SECURE_NO_WARNINGS

LIBS_PATH=-L .\dependencies
LIBS=-l GLEW\glew32s.lib -l GLFW\glfw3.lib -l msvcrt.lib
WIN_API_LIBS=-l opengl32.lib -l gdi32.lib -l user32.lib -l shell32.lib

EXECUTABLE=output.exe

CC=clang++


GCH:=$(shell git log --format=%H -n1)
GCM=$(shell git log --format=%s -n1)
GCB=$(shell git branch --show current)
GCD=$(shell git log --format=%ad -n1 --date=iso)
PPFLAGS+= -DCOMMIT_HASH="\"$(GCH)\"" -DCOMMIT_NAME="\"$(GCM)\"" -DCOMMIT_BRANCH="\"$(GCB)\"" -DCOMMIT_DATE="\"$(GCD)\""

SOURCES_10=$(wildcard $(SOURCE_DIR)/*.cpp)
SOURCES_20=$(SOURCES_10:$(SOURCE_DIR)/%=%)
SOURCES_LOC=$(filter-out $(CPP_EXCLUDES),$(SOURCES_20))
OBJECT_FILES_0=$(addprefix $(OBJECT_DIR)/,$(SOURCES_LOC))
OBJECT_FILES=$(OBJECT_FILES_0:.cpp=.o)

build-run: $(EXECUTABLE)
	$(EXECUTABLE)

build: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECT_FILES)
	@echo building $@
	@ $(CC) $(LIBS_PATH) $(LIBS) $(WIN_API_LIBS) $(OBJECT_FILES) -Xlinker /NODEFAULTLIB:libcmt -o "$@"

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@echo compiling $<
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	@ $(CC) $(PPFLAGS) $(INCLUDES) $(CFLAGS) "$<" -o "$@"

clear:
	@if exist "$(OBJECT_DIR)" rmdir /s /q $(OBJECT_DIR)
	@if exist "$(EXECUTABLE)" @del $(EXECUTABLE)


SHADERS_10=$(wildcard $(SHADERS_DIR)/*.frag)
SHADERS_20=$(wildcard $(SHADERS_DIR)/*.vert)
SHADERS=$(SHADERS_10) $(SHADERS_20)

.FORCE:
$(SHADERS_DIR)/%: .FORCE
	@echo validating $@
	@ dependencies/glslangValidator.exe "$@"

glslValidate: $(SHADERS)
