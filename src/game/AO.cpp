#include"AO.h"
#include"Area.h"
#include"MiscChunk.h"
#include"Position.h"
#include"Units.h"
#include"BlockProperties.h"

//create mask containing info about 8 blocks around a vertex
template<bool inChunk = false>
static uint8_t calcAO(chunk::Chunk chunk, pCube const cubeCoordInChunk) {
	auto &chunks{ chunk.chunks() };
	
	uint8_t cubes{};
	for(int j{}; j < chunk::ChunkAO::dirsCount; j ++) {
		auto const offsetCubePos{ cubeCoordInChunk + chunk::ChunkAO::dirsForIndex(j).min(0)/*-1, 0*/ };
		
		chunk::Chunk offsetCubeChunk;
		if constexpr(inChunk) {
			offsetCubeChunk = chunk;
		} 
		else {
			auto const offsetCubeChunkIndex{ chunk::MovingChunk{chunk}.offseted(offsetCubePos.valAs<pChunk>()).getIndex() };
			if(!offsetCubeChunkIndex.is()) continue;
			offsetCubeChunk = chunks[offsetCubeChunkIndex.get()];
		}
		
		auto const offsetedCube{ offsetCubeChunk.data().cubeAt2(offsetCubePos.in<pos::Chunk>()) };
		cubes = cubes | (int(useInAO(offsetedCube)) << j);
	}
	
	return cubes;
}

static void updateAOInChunks(chunk::Chunk origChunk, pCube const firstRel, pCube const lastRel) {	
	auto &chunks{ origChunk.chunks() };
	pChunk const origChunkPos{ origChunk.position() };	
	
	auto const fCube{ firstRel + origChunkPos };
	auto const lCube{ lastRel  + origChunkPos };
	
	iterateChunks(origChunk, fCube.as<pChunk>(), lCube.as<pChunk>(), [&](chunk::Chunk chunk, pChunk const chunkPos) {
		auto const chunkCubeCoord{ chunkPos.valAs<pCube>() };
		
		auto const updateArea{ intersectAreas3i(
			{ 0                           , units::cubesInChunkDim-1 },
			{ fCube.val() - chunkCubeCoord, lCube.val() - chunkCubeCoord }
		) };
		
		auto const f{ updateArea.first };
		auto const l{ updateArea.last  };
		
		iterateArea(f.max(1), l, [&](vec3i const coord) {
			auto startChunk{ chunk };
			pCube const cubeCoordInChunk{ coord };
			startChunk.ao()[cubeCoordInChunk] = calcAO</*InChunk=*/true>(startChunk, cubeCoordInChunk);
		});
		
		static constexpr vec3i axis[]{ {1,0,0}, {0,1,0}, {0,0,1} };
		static constexpr int axisCount{ 3 };
		static constexpr vec3i::value_type value = 0; //0'th coordinate

		//x=0, y=0. x=0, z=0. y=0, z=0 and other are calculated multiple times
		for(int i{}; i < axisCount; i++) {
			auto const mask{ axis[i] };
			
			if(f.dot(mask) <= value) {
				for(auto z{f.z}; z <= misc::lerp(l.z, value, mask.z); z++)
				for(auto y{f.y}; y <= misc::lerp(l.y, value, mask.y); y++)
				for(auto x{f.x}; x <= misc::lerp(l.x, value, mask.x); x++) {
					pCube const cubePosInChunk{ x, y, z };
					
					chunk.ao()[cubePosInChunk] = calcAO(chunk, cubePosInChunk);
				}
			}
		}
	});
}

void updateAOInArea(chunk::Chunk const origChunk, pCube const first, pCube const last) {
	if(Area{first.val(), last.val()}.isEmpty()) return;
	
	auto const volume{ (last - first).val() };
	if(volume.x * volume.y * volume.z > units::cubesInChunkDim * 5/*arbitrary number*/) {
		updateAOInChunks(origChunk, first, last);
	}
	else {
		auto &chunks{ origChunk.chunks() };
		
		iterateArea(first.val(), last.val(), [&](vec3i const coord) {
			pCube const startCoord{ coord };
			auto const startChunkIndex{ chunk::Move_to_neighbour_Chunk{origChunk}.moveToNeighbour(startCoord.valAs<pChunk>()).get() };
			if(startChunkIndex == -1) return;
			
			auto startChunk{ chunks[startChunkIndex] };
			auto const cubeCoordInChunk{ startCoord.in<pChunk>() };
			startChunk.ao()[cubeCoordInChunk] = calcAO(startChunk, cubeCoordInChunk);
		});
	}
}