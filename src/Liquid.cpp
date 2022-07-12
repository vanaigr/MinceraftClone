#include"Liquid.h"
#include"Area.h"
#include"BlocksData.h"

#include<iostream>
#include<unordered_set>
#include<functional>
#include<stdint.h>
#include<type_traits>
#include<algorithm>
#include<array>

struct ChunkAndCubeHash { 
	inline size_t operator()(chunk::ChunkAndCube const it) const noexcept {
		static_assert(sizeof(it.chunkIndex) == 4);
		static_assert(sizeof(it.cubeIndex) == 2);
		return std::hash<uint64_t>{}(it.chunkIndex | (uint64_t(it.cubeIndex) << 32) | (uint64_t(it.cubeIndex) << 48));
	} 
};

static constexpr chunk::LiquidCube::level_t maxLiquidToNeighbour{8};

void ChunksLiquidCubes::update() {
	auto &chunks{ this->chunks() };
	auto &gen{ gens[genIndex] };
	auto &genNext{ gens[(genIndex+1) % gensCount] };
	
	genNext.clear();
	
	std::unordered_set<chunk::ChunkAndCube, ChunkAndCubeHash> s{};
	for (auto const i : gen) s.insert(i); //a lot of memory allocations
	
	gen.assign(s.begin(), s.end());
	std::sort(gen.begin(), gen.end());
	
	chunk::OptionalChunkIndex prevChunk{}; //no index if previous chunk is not updated
	static chunk::BlocksArray<bool> blocksUpdatedInPrevChunk{};
	
	auto const updatePrevChunkData = [&]() {
		if(prevChunk.is()) {
			static constexpr auto dim{ units::blocksInChunkDim + 2 }; //+ 2 neighbours from neighbouring chunks
			static std::array<bool, pos::cubed(dim)> updatedBlocksNeighbours; 
			auto const blockToIndex = [](pBlock const pos) {
				auto const coord{ pos.val() };
				assert(coord.in(-1, units::blocksInChunkDim-1 + 1).all());
				
				return (coord.x+1) + (coord.y+1) * dim + (coord.z+1) * dim * dim;
			};
			auto const indexToBlock = [](int const index) {
				assert(index >= 0);
				
				auto const result{ pBlock{index % dim, (index / dim) % dim, (index / dim/ dim) % dim} - 1 };
				assert(result.val().in(-1, units::blocksInChunkDim-1 + 1).all());
				return result;
			};
			
			
			updatedBlocksNeighbours.fill({});
			
			auto prevChunkChunk{ chunks[prevChunk.get()] };
			auto &aabb{ prevChunkChunk.aabb() };
			auto start{ aabb.start() };
			auto end  { aabb.end  () };		
			
			for(int blockI{}; blockI < decltype(blocksUpdatedInPrevChunk)::size; blockI++) {
				if(!blocksUpdatedInPrevChunk[blockI]) continue;
				
				auto const blockCoord{ chunk::blockIndexToCoord(blockI) };
				
				iterate3by3Volume([&](vec3i const neighbourDir, int const index) {
					auto const offsetedBlockCoord{ blockCoord + neighbourDir };
					updatedBlocksNeighbours[blockToIndex(offsetedBlockCoord)] = true;
				});
	
				updateBlockDataWithoutNeighbours(prevChunkChunk, blockCoord);
				start = start.min(blockCoord.val());
				end   = end  .max(blockCoord.val());
			}
			
			aabb = { start, end };
			
			
			for(int blockI{}; blockI < int(updatedBlocksNeighbours.size()); blockI++) {
				if(!updatedBlocksNeighbours[blockI]) continue;
				
				auto const blockCoord{ indexToBlock(blockI) };
				auto const blockChunkOffsetCoord{ blockCoord.as<pChunk>() };
				auto const blockInChunkCoord{ blockCoord.in<pChunk>() };
				
				auto const neighbourChunkIndex{ chunk::MovingChunk{ prevChunkChunk }.offseted(blockChunkOffsetCoord.val()).getIndex() };
				if(!neighbourChunkIndex.is()) continue;
				auto neighbourChunk{ chunks[neighbourChunkIndex.get()] };
				
				updateBlockDataNeighboursInfo(neighbourChunk, blockInChunkCoord);
				if(blockChunkOffsetCoord == 0) /*current chunk status is already set*/;
				else neighbourChunk.status().setBlocksUpdated(true);
			}
			
			prevChunkChunk.status().setBlocksUpdated(true);
		}
		
		prevChunk = {};
		blocksUpdatedInPrevChunk.reset();
	};
	
	for(size_t i{}, size{ gen.size() }; i < size; i++) {
		auto const &cubePosData{ gen[i] };
		auto const &[chunkIndex, cubeIndex] = cubePosData;
		
		if(chunk::OptionalChunkIndex{chunkIndex} != prevChunk) {
			updatePrevChunkData();
		}

		auto chunk{ chunks[chunkIndex] };
		auto const cubeCoord{ chunk::cubeIndexToCoord(cubeIndex) };
	
		auto &liquidCube{ chunk.liquid()[cubeIndex] };
		auto const id{ liquidCube.id }; //copy
		auto level{ liquidCube.level }; //copy
		
		auto modified{ false };
		auto keepUpdating{ false };
		
		//move liquid downwards
		if(level != 0) [&]() {
			auto const neighbourCubePosData = getNeighbourCube(chunk, cubeCoord, vec3i{0, -1, 0});
			auto const [neighbourCubeChunkIndex, neighbourCubeCubeIndex] = neighbourCubePosData; 
			if(neighbourCubeChunkIndex == -1) return;
			
			auto neighbourCubeChunk{ chunks[neighbourCubeChunkIndex] };
			auto const neighbourCubeCoord{ chunk::cubeIndexToCoord(neighbourCubeCubeIndex) };
			if(neighbourCubeChunk.data().cubeAt(neighbourCubeCoord).isSolid) return;
			
			auto &neighbourLiquidCube{ neighbourCubeChunk.liquid()[neighbourCubeCubeIndex] };
			auto const toMax( chunk::LiquidCube::maxLevel - neighbourLiquidCube.level );
			
			if(toMax != 0 && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == id)) {
				auto diff{ std::min<int>(level, toMax) };
				//if(diff > maxLiquidToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; } //limit on downwards flow
				
				modified = true;
				level -= diff;		
				neighbourLiquidCube = { id, neighbourLiquidCube.level + diff };
				
				genNext.push_back(neighbourCubePosData);
				blocksUpdatedInPrevChunk[neighbourCubeCoord.as<pBlock>()] = true;
				return;
			}
			
			return;
		}();
		
		//move liquid sideways(?)
		if(level != 0) [&]() {
			struct LiquidCubeAndPos { 
				chunk::LiquidCube liquidCube; //copy
				chunk::LiquidCube *liquidCubeLoc; 
				chunk::ChunkAndCube posData; 
				bool modified; 
				bool valid; 
			};
			
			static constexpr int sideNeighboursCount = 4;
			static constexpr vec3i dirs[sideNeighboursCount] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
			
			LiquidCubeAndPos cubes[sideNeighboursCount] = {};
			
			//fill the neighbours
			for(int i{}; i < sideNeighboursCount; i++) {
				auto const dir{ dirs[i] };
				
				auto const neighbourCubePosData = getNeighbourCube(chunk, cubeCoord, dir);
				auto const [cubeNeighbourChunkIndex, cubeNeighbourCubeIndex] = neighbourCubePosData; 
				if(cubeNeighbourChunkIndex == -1) continue;
				
				auto cubeNeighbourChunk{ chunks[cubeNeighbourChunkIndex] };
				auto const cubeNeighbourCoord{ chunk::cubeIndexToCoord(cubeNeighbourCubeIndex) };
				if(cubeNeighbourChunk.data().cubeAt(cubeNeighbourCoord).isSolid) continue;
				
				auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[cubeNeighbourCubeIndex] };
				if((level > neighbourLiquidCube.level) && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == id)) {
					cubes[i] = { neighbourLiquidCube, &neighbourLiquidCube, neighbourCubePosData, false, true };
				}
			}
			
			//average out liquid levels between current cube and its neighbours
			while(true) {
				int validCubesCount{};
				int liquidTotal{ level };
				for(int i{}; i < sideNeighboursCount; i++) {
					auto &cube{ cubes[i] };
					if(!cube.valid) continue;
					validCubesCount++;
					liquidTotal += cube.liquidCube.level;
				}
				
				if(validCubesCount == 0) break;
				
				auto const avgLevel{ liquidTotal / (validCubesCount+1) }; //this variant leaves MORE liquid in current cube
				
				int movedToNeighbours{ 0 };
				
				for(int i{}; i < sideNeighboursCount; i++) {
					auto &cube{ cubes[i] };
					if(!cube.valid) continue;
					
					auto diff{ avgLevel - cube.liquidCube.level };
					auto const maxToNeighbour{ diff > maxLiquidToNeighbour };
					if(maxToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; }
					
					if(diff > 0) {
						movedToNeighbours += diff;
						cube.liquidCube = { id, cube.liquidCube.level + diff };
						cube.modified = true;
					}
					
					if(diff <= 0 || maxToNeighbour) cube.valid = false;
				}
				
				if(movedToNeighbours != 0) {
					level -= movedToNeighbours;
					modified = true;
				}
			}
			
			//write modified cubes
			for(int i{}; i < sideNeighboursCount; i++) {
				auto &cube{ cubes[i] };
				if(cube.modified) {
					*cube.liquidCubeLoc = cube.liquidCube;
					genNext.push_back(cube.posData);
					auto const cubeCoord{ chunk::cubeIndexToCoord(cube.posData.cubeIndex) };
					blocksUpdatedInPrevChunk[cubeCoord.as<pBlock>()] = true;
				}
			}
		}();
		
		if(modified) {
			liquidCube = { id, level };
			prevChunk = chunkIndex;
			blocksUpdatedInPrevChunk[cubeCoord.as<pBlock>()] = true;
		}
		if(keepUpdating) {
			genNext.push_back(cubePosData);
		}
		
		//mark cubes that could move liquid to this cube as needing pudate
		if(level < chunk::LiquidCube::maxLevel) {
			auto const fromAbove = [&]() {
				auto const neighbourCubePosData = getNeighbourCube(chunk, cubeCoord, vec3i{0, 1, 0});
				auto const [cubeUpChunkIndex, cubeUpCubeIndex] = neighbourCubePosData; 
				if(cubeUpChunkIndex == -1) return false;
				
				auto cubeUpChunk{ chunks[cubeUpChunkIndex] };
				auto const cubeUpCoord{ chunk::cubeIndexToCoord(cubeUpCubeIndex) };
				if(cubeUpChunk.data().cubeAt(cubeUpCoord).isSolid) return false ;
				
				auto &upLiquidCube{ cubeUpChunk.liquid()[cubeUpCubeIndex] };
				if(upLiquidCube.id == id) {
					genNext.push_back(neighbourCubePosData);
					return true;
				}
				return false;
			}();
			
			if(!fromAbove) {
				static constexpr vec3i dirs[] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
				for(int i{}; i < 4; i++) {
					auto const dir{ dirs[i] };
					
					auto const neighbourCubePosData = getNeighbourCube(chunk, cubeCoord, dir);
					auto const [cubeNeighbourChunkIndex, cubeNeighbourCubeIndex] = neighbourCubePosData; 
					if(cubeNeighbourChunkIndex == -1) continue;
					
					auto cubeNeighbourChunk{ chunks[cubeNeighbourChunkIndex] };
					auto const cubeNeighbourCoord{ chunk::cubeIndexToCoord(cubeNeighbourCubeIndex) };
					if(cubeNeighbourChunk.data().cubeAt(cubeNeighbourCoord).isSolid) continue;
					
					auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[cubeNeighbourCubeIndex] };
					if(neighbourLiquidCube.level > level && neighbourLiquidCube.id == id) {
						genNext.push_back(neighbourCubePosData);
					}
				}
			}
		}
	}
	
	updatePrevChunkData();
	
	genIndex = (genIndex+1) % gensCount;
}