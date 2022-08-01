INCLUDES=-I .\dependencies\include -I .\src
SOURCE_DIR=src
SHADERS_DIR=shaders
OBJECT_DIR=obj
WS= -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-private-field
CFLAGS= -O3 -std=c++17 -pedantic -c $(WS) -mssse3
PPFLAGS=-D GLEW_STATIC -D GLEW_NO_GLU -D _CRT_SECURE_NO_WARNINGS

LIBS_PATH=-L .\dependencies
LIBS=-l GLEW\glew32s.lib -l GLFW\glfw3.lib -l msvcrt.lib
WIN_API_LIBS=-l opengl32.lib -l gdi32.lib -l user32.lib -l shell32.lib

GAME_EXECUTABLE=game.exe
UPDATE_FONT_EXECUTABLE=updateFont.exe
SDF_EXECUTABLE=sdf.exe

CC=clang++


GCH:=$(shell git log --format=%H -n1)
GCM=$(shell git log --format=%s -n1)
GCB=$(shell git branch --show current)
GCD=$(shell git log --format=%ad -n1 --date=iso)
PPFLAGS+= -DCOMMIT_HASH="\"$(GCH)\"" -DCOMMIT_NAME="\"$(GCM)\"" -DCOMMIT_BRANCH="\"$(GCB)\"" -DCOMMIT_DATE="\"$(GCD)\""

SOURCE_FILES=$(subst $(SOURCE_DIR)/,,$(wildcard $(SOURCE_DIR)/**/*.cpp))
#OBJECT_FILES=$(subst .cpp,.o,$(addprefix $(OBJECT_DIR)/,$(SOURCE_FILES)))


define MAKE_EXECUTABLE=
$(1)_OBJECT_FILES=$(subst .cpp,.o,$(addprefix $(OBJECT_DIR)/,$(2)))
$(1): $$($(1)_OBJECT_FILES)
	@echo building $(1)
	@ $(CC) $(LIBS_PATH) $(LIBS) $(WIN_API_LIBS) $$($(1)_OBJECT_FILES) -Xlinker /NODEFAULTLIB:libcmt -o "$(1)"
	$(3)
endef


./assets/font.fnt:
	@echo ERROR: could not find font description file: $@
	@exit -1
./assets/font.bmp:
	@echo ERROR: could not find font bitmap
	@if exist "./assets/font.png" @echo bitmap can be made from the "./assets/font.png" file
	@exit -1

.PHONY: update_font update_sdf

update_font ./assets/font.txt:$(eval $(call MAKE_EXECUTABLE,$(UPDATE_FONT_EXECUTABLE),UpdateFont.cpp,))\
								./assets/font.fnt $(UPDATE_FONT_EXECUTABLE)
	@echo updating font file						
	@$(UPDATE_FONT_EXECUTABLE)
	
update_sdf ./assets/sdfFont.bmp:$(eval $(call MAKE_EXECUTABLE,$(SDF_EXECUTABLE),$(filter SDF/% font/% image/%,$(SOURCE_FILES))))\
								./assets/font.txt ./assets/font.bmp $(SDF_EXECUTABLE)
	@echo updating SDF font image
	@$(SDF_EXECUTABLE)
	
build_game:: $(eval $(call MAKE_EXECUTABLE,$(GAME_EXECUTABLE),$(filter game/% font/% image/%,$(SOURCE_FILES))))\
			./assets/font.txt ./assets/sdfFont.bmp $(GAME_EXECUTABLE)


build-run:: build
	$(GAME_EXECUTABLE)

build:: build_game

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@echo compiling $<
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	@ $(CC) $(PPFLAGS) $(INCLUDES) $(CFLAGS) "$<" -o "$@"

clear::
	@if exist "$(OBJECT_DIR)" rmdir /s /q $(OBJECT_DIR)
	@if exist "$(GAME_EXECUTABLE)" @del $(GAME_EXECUTABLE)
	@if exist "$(UPDATE_FONT_EXECUTABLE)" @del $(UPDATE_FONT_EXECUTABLE)
	@if exist "$(SDF_EXECUTABLE)" @del $(SDF_EXECUTABLE)
	@if exist "./assets/font.txt" @del ".\assets\font.txt"
	@if exist "./assets/sdfFont.bmp" @del ".\assets\sdfFont.bmp"


SHADERS_10=$(wildcard $(SHADERS_DIR)/*.frag)
SHADERS_20=$(wildcard $(SHADERS_DIR)/*.vert)
SHADERS=$(SHADERS_10) $(SHADERS_20)

.FORCE:
$(SHADERS_DIR)/%: .FORCE
	@echo validating $@
	@ dependencies/glslangValidator.exe "$@"

glslValidate:: $(SHADERS)
