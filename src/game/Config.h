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
	
	vec2i groupSize;
};

void parseConfigFromFile(Config &dst);