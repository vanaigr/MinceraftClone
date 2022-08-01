#pragma once

#include"Chunk.h"
#include"Units.h"
#include"Position.h"

inline chunk::ChunkAndCube getNeighbourCube(
	chunk::Chunk cubeChunk, pCube const cubeCoord,
	vec3i const neighbourDir
) {	
	pCube const neighbourCubePos{ cubeCoord + pCube{neighbourDir} };
	auto const neighbourCubeChunkCoord{ neighbourCubePos.valAs<pos::Chunk>() };
	auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
	
	auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{cubeChunk}.offset(neighbourCubeChunkCoord).get() };
	if(neighbourCubeChunkIndex == -1) return { -1, 0 };

	return { neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) };
}

template<typename Action>
inline void iterateCubeNeighbours(
	chunk::Chunk cubeChunk, vec3i const cubeCoord, 
	Action &&action
) {
	auto &chunks{ cubeChunk.chunks() };
	auto const chunkCoord{ cubeChunk.position() };
	
	for(auto i{decltype(chunk::ChunkLighting::dirsCount){}}; i < chunk::ChunkLighting::dirsCount; i++) {
		auto const neighbourDir{ chunk::ChunkLighting::indexAsDir(i) };
		auto const neighbourCube{ getNeighbourCube(cubeChunk, pCube{cubeCoord}, neighbourDir) };
		if(neighbourCube.chunkIndex == -1) continue;

		action(neighbourDir, chunks[neighbourCube.chunkIndex], chunk::cubeIndexToCoord(neighbourCube.cubeIndex).val());
	}
}

template<typename Action>
inline void iterateChunks(chunk::Chunk const startChunk, pChunk const first, pChunk const last, Action &&action) {
	auto const fChunk{ first.val() };
	auto const lChunk{ last .val() };
	chunk::MovingChunk zMTNChunk{ startChunk };
	
	for(auto cz{ fChunk.z }; cz <= lChunk.z; cz++) {
		zMTNChunk = zMTNChunk.moved({fChunk.x, fChunk.y, cz});
		auto yMTNChunk{ zMTNChunk };
		
	for(auto cy{ fChunk.y }; cy <= lChunk.y; cy++) {
		yMTNChunk = yMTNChunk.moved({fChunk.x, cy, cz});
		auto xMTNChunk{ zMTNChunk };
		
	for(auto cx{ fChunk.x }; cx <= lChunk.x; cx++) {
		xMTNChunk = xMTNChunk.moved({cx, cy, cz});
		if(!xMTNChunk.is()) continue;
		
		action(xMTNChunk.get(), {cx, cy, cz});
	}}}
}

template<typename Action>
inline void iterateAllChunks(chunk::Chunk const startChunk, pChunk const first, pChunk const last, Action &&action) {
	auto const fChunk{ first.val() };
	auto const lChunk{ last .val() };
	chunk::MovingChunk zMTNChunk{ startChunk };
	
	for(auto cz{ fChunk.z }; cz <= lChunk.z; cz++) {
		zMTNChunk = zMTNChunk.moved({fChunk.x, fChunk.y, cz});
		auto yMTNChunk{ zMTNChunk };
		
	for(auto cy{ fChunk.y }; cy <= lChunk.y; cy++) {
		yMTNChunk = yMTNChunk.moved({fChunk.x, cy, cz});
		auto xMTNChunk{ zMTNChunk };
		
	for(auto cx{ fChunk.x }; cx <= lChunk.x; cx++) {
		xMTNChunk = xMTNChunk.moved({cx, cy, cz});
		
		action(xMTNChunk.getIndex().get(), {cx, cy, cz});
	}}}
}