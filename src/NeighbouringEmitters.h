#pragma once

#include"Chunk.h"
#include"Vector.h"
#include"Units.h"
#include"MiscChunk.h"

inline void updateNeighbouringEmitters(chunk::Chunk chunk) {
	auto &chunks{ chunk.chunks() };
	struct NeighbourChunk {
		vec3i dir;
		int chunkIndex;
		int emittersCount;
	};
				
	NeighbourChunk neighbours[3*3*3];
	int neighboursCount{};
	int totalEmittersCount{};
	
	iterate3by3Volume([&](vec3i const neighbourDir, int const i) {
		auto const neighbourIndex{ chunk::Move_to_neighbour_Chunk{ chunk }.moveToNeighbour(neighbourDir) };
		if(neighbourIndex.is()) {
			auto const neighbourChunk{ chunks[neighbourIndex.get()] };
			auto const emittersCount{ neighbourChunk.emitters().size() };
			
			totalEmittersCount += emittersCount;
			neighbours[neighboursCount++] = { neighbourDir, neighbourChunk.chunkIndex(), emittersCount };
		}
	});
	
	auto &curChunkNeighbouringEmitters{ chunk.neighbouringEmitters() };
	if(totalEmittersCount == 0) { curChunkNeighbouringEmitters.clear(); return; }
	
	std::array<vec3i, chunk::Chunk3x3BlocksList::capacity> neighbouringEmitters{};
	int const neighbouringEmittersCount{ std::min<int>(neighbouringEmitters.size(), totalEmittersCount) };
	int const emitterStep{ std::max<int>(totalEmittersCount / neighbouringEmitters.size(), 1) };
	
	int neighbouringEmitterToAdd( emitterStep > 1 ? totalEmittersCount % neighbouringEmitters.size() : 0 );
	int startChunkEmiter{};
	int currentEmitterNeighbourIndex{};
	for(int i{}; i < neighbouringEmittersCount;) {
		assert(currentEmitterNeighbourIndex < neighboursCount);
		auto const neighbour{ neighbours[currentEmitterNeighbourIndex] };
		
		if(neighbouringEmitterToAdd < startChunkEmiter + neighbour.emittersCount) {
			auto const localChunkEmitterIndex{ neighbouringEmitterToAdd - startChunkEmiter };
			auto const localChunkEmitterBlock{ chunks[neighbour.chunkIndex].emitters()(localChunkEmitterIndex) };
			neighbouringEmitters[i] = localChunkEmitterBlock + neighbour.dir * units::blocksInChunkDim;
			neighbouringEmitterToAdd += emitterStep;
			i++;
		}
		else {
			startChunkEmiter += neighbour.emittersCount;
			currentEmitterNeighbourIndex++;
		}
	}
	
	curChunkNeighbouringEmitters.fillRepeated(neighbouringEmitters, neighbouringEmittersCount);
}



static void setChunksUpdateNeighbouringEmitters(chunk::Chunk chunk) {
	auto &chunks{ chunk.chunks() };
	iterate3by3Volume([&](vec3i const dir, int const i) {
		auto const neighbourIndex{ chunk::Move_to_neighbour_Chunk{chunk}.moveToNeighbour(dir).get() };
		if(neighbourIndex != -1) {
			chunks[neighbourIndex].status().setUpdateNeighbouringEmitters(true);
		}
	});
}