INCLUDES=-I .\dependencies\include
SOURCE_DIR=src
OBJECT_DIR=obj
CPP_EXCLUDES=  

CFLAGS=-m64 -O3 -std=c++17 -pedantic -c -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unneeded-internal-declaration -Wno-unused-function -Wno-unused-label -fuse-ld=lld-link
PPFLAGS=-D GLEW_STATIC -D GLEW_NO_GLU -D _CRT_SECURE_NO_WARNINGS

LIBS_PATH=-L .\dependencies
LIBS=-l GLEW\glew32s.lib -l GLFW\glfw3.lib -l msvcrt.lib
WIN_API_LIBS=-l opengl32.lib -l gdi32.lib -l user32.lib -l shell32.lib

EXECUTABLE=output.exe


CC=clang++

CUR_PATH_0=$(shell cd)
CUR_PATH=$(abspath $(CUR_PATH_0))
SOURCES_0=$(shell dir $(SOURCE_DIR)\*.cpp /s /b)
SOURCES_10=$(abspath $(SOURCES_0))
SOURCES_20=$(SOURCES_10:$(CUR_PATH)/$(SOURCE_DIR)/%=%)
SOURCES_LOC=$(filter-out $(CPP_EXCLUDES),$(SOURCES_20))
SOURCES=$(addprefix $(SOURCE_DIR)/,$(SOURCES_LOC))

OBJECT_FILES_0=$(addprefix $(OBJECT_DIR)/,$(SOURCES_LOC))
OBJECT_FILES=$(OBJECT_FILES_0:.cpp=.o)


build-run: $(EXECUTABLE)
	$(EXECUTABLE)
	
build: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES) $(OBJECT_FILES)
	@echo -------------------------------------------------
	@echo building $@:
	$(CC) $(LIBS_PATH) $(LIBS) $(WIN_API_LIBS) $(OBJECT_FILES) -o "$@"

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@echo -------------------------------------------------
	@echo compiling $<:
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(PPFLAGS) $(INCLUDES) $(CFLAGS) "$<" -o "$@"
	
clear-obj: 
	@if exist "$(OBJECT_DIR)" rmdir $(OBJECT_DIR) /s
	
clear: clear-obj
	@if exist "$(OBJECT_DIR)" rmdir $(EXECUTABLE)
	

#run: build