#pragma once
#include"Vector.h"

struct Config {
	int viewDistance;
	bool loadChunks;
	bool saveChunks;
	double playerCameraFovDeg;
	vec2d mouseSensitivity;
	int chunkUpdatesPerFrame;
	bool lockFramerate;
	vec2i screenshotSize;
};

void parseConfigFromFile(Config &dst);