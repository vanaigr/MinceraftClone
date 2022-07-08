#include"Liquid.h"
#include"Area.h"

#include<iostream>
#include<unordered_set>
#include<functional>
#include<stdint.h>
#include<type_traits>
#include<algorithm>

struct ChunkAndCubeHash { 
	inline size_t operator()(chunk::ChunkAndCube const it) const noexcept {
		static_assert(sizeof(it.chunkIndex) == 4);
		static_assert(sizeof(it.cubeIndex) == 2);
		return std::hash<uint64_t>{}(it.chunkIndex | (uint64_t(it.cubeIndex) << 32) | (uint64_t(it.cubeIndex) << 48));
	} 
};

//static constexpr chunk::LiquidCube::level_t maxMovePerFrame{8};

void ChunksLiquidCubes::update() {
	auto &chunks{ this->chunks() };
	auto &gen{ gens[genIndex] };
	auto &genNext{ gens[(genIndex+1) % gensCount] };
	
	genNext.clear();
	
	std::unordered_set<chunk::ChunkAndCube, ChunkAndCubeHash> s{};
	for (auto const i : gen) s.insert(i);
	
	gen.assign(s.begin(), s.end());
	std::sort(gen.begin(), gen.end());
	
	chunk::OptionalChunkIndex prevChunk{}; //no index if previous chunk is not updated
	
	for(auto const &[chunkIndex, cubeIndex] : gen) {
		if(prevChunk.is() && chunk::OptionalChunkIndex{chunkIndex} != prevChunk) {
			chunks[prevChunk.get()].status().setBlocksUpdated(true);
			prevChunk = {};
		}
		
		auto chunk{ chunks[chunkIndex] };
		auto const cubeCoord{ chunk::cubeIndexToCoord(cubeIndex) };
	
		auto &liquidCube{ chunk.liquid()[cubeIndex] };
		auto const id{ liquidCube.id }; //copy!
		auto level{ liquidCube.level }; //copy
		
		if(id == 0) continue;
		
		auto const propDown = [&]() {
			auto const neighbourChunkAndCube = getNeighbourCube(chunk, cubeCoord, vec3i{0, -1, 0});
			auto const [cubeDownChunkIndex, cubeDownCubeIndex] = neighbourChunkAndCube; 
			if(cubeDownChunkIndex == -1) return false;
			
			auto cubeDownChunk{ chunks[cubeDownChunkIndex] };
			auto const cubeDownCoord{ chunk::cubeIndexToCoord(cubeDownCubeIndex) };
			if(cubeDownChunk.data().cubeAt(cubeDownCoord).isSolid) return false;
			
			auto &downLiquidCube{ cubeDownChunk.liquid()[cubeDownCubeIndex] };
			chunk::LiquidCube::level_t const toMax( chunk::LiquidCube::maxLevel - downLiquidCube.level );
			if(toMax != 0 && (downLiquidCube.id == 0 || downLiquidCube.id == id)) {
				auto const diff{ std::min({level, toMax}) };
				level -= diff;		
				downLiquidCube = { id, downLiquidCube.level + diff };
				
				genNext.push_back(neighbourChunkAndCube);
				return true;
			}
			
			return false;
		}();
		
		if(propDown) {
			liquidCube = { id, level };
			prevChunk = chunkIndex;
		}
		else {
			auto const propSides = [&]() {
				struct LiquidCubeAndPos { chunk::LiquidCube liquidCube; chunk::LiquidCube *liquidCubeLoc; chunk::ChunkAndCube pos; bool modified; bool valid; };
				LiquidCubeAndPos cubes[4] = {};
				
				static constexpr vec3i dirs[] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
				for(int i{}; i < 4; i++) {
					auto const dir{ dirs[i] };
					
					auto const neighbourChunkAndCube = getNeighbourCube(chunk, cubeCoord, dir);
					auto const [cubeNeighbourChunkIndex, cubeNeighbourCubeIndex] = neighbourChunkAndCube; 
					if(cubeNeighbourChunkIndex == -1) continue;
					
					auto cubeNeighbourChunk{ chunks[cubeNeighbourChunkIndex] };
					auto const cubeNeighbourCoord{ chunk::cubeIndexToCoord(cubeNeighbourCubeIndex) };
					if(cubeNeighbourChunk.data().cubeAt(cubeNeighbourCoord).isSolid) continue;
					
					auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[cubeNeighbourCubeIndex] };
					if((level > neighbourLiquidCube.level) && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == id)) {
						cubes[i] = { neighbourLiquidCube, &neighbourLiquidCube, neighbourChunkAndCube, false, true };
					}
				}
				
				auto modified{ false };
				
				while(true) {
					int validCubesCount{};
					int liquidTotal{ level };
					for(int i{}; i < 4; i++) {
						auto &cube{ cubes[i] };
						if(!cube.valid) continue;
						validCubesCount++;
						liquidTotal += cube.liquidCube.level;
					}
					
					if(validCubesCount == 0) break;
					
					auto const avgLevel{ liquidTotal / (validCubesCount+1) }; //this variant leaves MORE liquid in current cube
					
					int movedToNeighbours{ 0 };
					
					for(int i{}; i < 4; i++) {
						auto &cube{ cubes[i] };
						if(!cube.valid) continue;
						
						auto const diff{ avgLevel - cube.liquidCube.level };
						if(diff > 0) {
							movedToNeighbours += diff;
							cube.liquidCube = { id, cube.liquidCube.level + diff };
							cube.modified = true;
						}
						else cube.valid = false;
					}
					
					if(movedToNeighbours != 0) {
						level -= movedToNeighbours;
						modified = true;
					}
				}
				
				for(int i{}; i < 4; i++) {
					auto &cube{ cubes[i] };
					if(cube.modified) {
						*cube.liquidCubeLoc = cube.liquidCube;
						genNext.push_back(cube.pos);
					}
				}
				
				return modified;
			}();
			if(propSides) {
				liquidCube = { id, level };
				prevChunk = chunkIndex;
			}
		}
		
		if(level < chunk::LiquidCube::maxLevel) {
			auto const propFromUp = [&]() {
				auto const neighbourChunkAndCube = getNeighbourCube(chunk, cubeCoord, vec3i{0, 1, 0});
				auto const [cubeUpChunkIndex, cubeUpCubeIndex] = neighbourChunkAndCube; 
				if(cubeUpChunkIndex == -1) return false;
				
				auto cubeUpChunk{ chunks[cubeUpChunkIndex] };
				auto const cubeUpCoord{ chunk::cubeIndexToCoord(cubeUpCubeIndex) };
				if(cubeUpChunk.data().cubeAt(cubeUpCoord).isSolid) return false ;
				
				auto &upLiquidCube{ cubeUpChunk.liquid()[cubeUpCubeIndex] };
				if(upLiquidCube.id == id) {
					genNext.push_back(neighbourChunkAndCube);
					return true;
				}
				return false;
			}();
			
			if(!propFromUp) {
				static constexpr vec3i dirs[] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
				for(int i{}; i < 4; i++) {
					auto const dir{ dirs[i] };
					
					auto const neighbourChunkAndCube = getNeighbourCube(chunk, cubeCoord, dir);
					auto const [cubeNeighbourChunkIndex, cubeNeighbourCubeIndex] = neighbourChunkAndCube; 
					if(cubeNeighbourChunkIndex == -1) continue;
					
					auto cubeNeighbourChunk{ chunks[cubeNeighbourChunkIndex] };
					auto const cubeNeighbourCoord{ chunk::cubeIndexToCoord(cubeNeighbourCubeIndex) };
					if(cubeNeighbourChunk.data().cubeAt(cubeNeighbourCoord).isSolid) continue;
					
					auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[cubeNeighbourCubeIndex] };
					if(neighbourLiquidCube.level > level && neighbourLiquidCube.id == id) {
						genNext.push_back(neighbourChunkAndCube);
					}
				}
			}
		}
	}
	if(prevChunk.is()) {
		chunks[prevChunk.get()].status().setBlocksUpdated(true);
	}
	
	genIndex = (genIndex+1) % gensCount;
}