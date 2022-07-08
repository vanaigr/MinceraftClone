#pragma once
#include"Chunk.h"
#include<vector>

struct ChunksLiquidCubes {
private:
	static constexpr int gensCount = 2;
	chunk::Chunks *chunks_;
	std::vector<chunk::ChunkAndCube> gens[gensCount];
	int genIndex;
	
	chunk::Chunks &chunks() { return *chunks_; } 
public:
	ChunksLiquidCubes(chunk::Chunks &chunks) : 
		chunks_{ &chunks }, gens{}, genIndex{} {}
	
	void update();
	
	void add(chunk::ChunkAndCube const cube) {
		gens[genIndex].push_back(cube);
	}
};
