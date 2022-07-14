#pragma once
#include"Vector.h"

#include<string>


struct Config {
	int viewDistance;
	
	bool loadChunks;
	bool saveChunks;
	std::string worldName;
	
	double playerCameraFovDeg;
	vec2d mouseSensitivity;
	int chunkUpdatesPerFrame;
	
	bool lockFramerate;
	
	vec2i screenshotSize;
};

void parseConfigFromFile(Config &dst);