#pragma once
#include"Vector.h"

#include<string>


struct Config {
	int viewDistance;
	
	bool loadChunks;
	bool saveChunks;
	std::string worldName;
	
	double playerCameraFOV;
	vec2d mouseSensitivity;
	int chunkUpdatesPerFrame;
	
	bool lockFramerate;
};

void parseConfigFromFile(Config &dst);