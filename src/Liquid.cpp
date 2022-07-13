#include"Liquid.h"
#include"Area.h"
#include"MiscChunk.h"
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

template<typename T, int neighbours_>
struct ChunkBlocksWithNeighbours {
	using value_type = T;
	static constexpr auto neighbours{ neighbours_ };
	static constexpr auto dim{ units::blocksInChunkDim + neighbours*2 };
	static constexpr auto size{ pos::cubed(dim) };
	
	static constexpr bool checkIndexValid(int const index) {
		return index >= 0 && index < size;
	}
	static constexpr bool checkCoordValid(pBlock const coord) {
		return coord.val().in(-neighbours, units::blocksInChunkDim-1 + neighbours).all();
	}	
	static constexpr bool checkCoordValid2(pBlock const coord) {
		if(checkCoordValid(coord)) return true;
		else { std::cout << coord.val() << '\n'; assert(false); return false; }
	}
	
	static constexpr int blockToIndex(pBlock const coord) {
		assert(checkCoordValid(coord));
		return (coord.val() + neighbours).dot(vec3i(1, dim, dim*dim));
	};
	static constexpr pBlock indexToBlock(int const index) {
		assert(checkIndexValid(index));
		return pBlock{index % dim, (index / dim) % dim, (index / dim / dim) % dim} - neighbours;
	};
	
private:
	std::array<value_type, size> data; 
public:
	ChunkBlocksWithNeighbours() = default;
	explicit ChunkBlocksWithNeighbours(value_type const value) { fill(value); }
	
	value_type       &operator[](int const index)       { assert(checkIndexValid(index)); return data[index]; }
	value_type const &operator[](int const index) const { assert(checkIndexValid(index)); return data[index]; }
	
	value_type       &operator[](pBlock const coord)       { assert(checkCoordValid2(coord)); return (*this)[blockToIndex(coord)]; }
	value_type const &operator[](pBlock const coord) const { assert(checkCoordValid2(coord)); return (*this)[blockToIndex(coord)]; }
	
	void fill(value_type const value) { data.fill(value); }
	void reset() { data.fill(value_type()); }
};

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
	static ChunkBlocksWithNeighbours<bool, 1> blocksUpdatedInPrevChunk{};
	
	auto const updatePrevChunkData = [&]() {
		if(prevChunk.is()) {
			static ChunkBlocksWithNeighbours<bool, 2> updatedBlocksNeighbours{};
			updatedBlocksNeighbours.fill({});
			
			std::array<bool, pos::cubed(3)> neighbourChunksUpdated{};
			
			
			auto prevChunkChunk{ chunks[prevChunk.get()] };
			
			//update BlockData block state
			for(int blockI{}; blockI < blocksUpdatedInPrevChunk.size; blockI++) {
				if(!blocksUpdatedInPrevChunk[blockI]) continue;
				
				auto const blockCoord{ blocksUpdatedInPrevChunk.indexToBlock(blockI) };
				auto const blockChunkOffsetCoord{ blockCoord.as<pChunk>().val() };
				auto const blockInChunkCoord{ blockCoord.in<pChunk>() };
				
				auto const neighbourChunkIndex{ chunk::MovingChunk{prevChunkChunk}.offseted(blockChunkOffsetCoord).getIndex() };
				assert(neighbourChunkIndex.is()); //chunk can't be invalid if one of its blocks was added to the list
				auto neighbourChunk{ chunks[neighbourChunkIndex.get()] };
				
				updateBlockDataWithoutNeighbours(neighbourChunk, blockCoord.in<pChunk>());
				
				iterate3by3Volume([&](vec3i const neighbourDir, int const index) {
					updatedBlocksNeighbours[blockCoord + neighbourDir] = true;
				});
				
				neighbourChunksUpdated[index3FromDir(blockChunkOffsetCoord)] = true;
			}
			
			//update BlockData neighbours state
			for(int blockI{}; blockI < updatedBlocksNeighbours.size; blockI++) {
				if(!updatedBlocksNeighbours[blockI]) continue;
				
				auto const blockCoord{ updatedBlocksNeighbours.indexToBlock(blockI) };
				auto const blockChunkOffsetCoord{ blockCoord.as<pChunk>().val() };
				auto const blockInChunkCoord{ blockCoord.in<pChunk>() };
				
				auto const neighbourChunkIndex{ chunk::MovingChunk{prevChunkChunk}.offseted(blockChunkOffsetCoord).getIndex() };
				if(!neighbourChunkIndex.is()) continue;
				auto neighbourChunk{ chunks[neighbourChunkIndex.get()] };
				
				updateBlockDataNeighboursInfo(neighbourChunk, blockInChunkCoord);
			}
			
			//update AABB and status
			iterate3by3Volume([&](vec3i const dir, int const index3) {
				if(!neighbourChunksUpdated[index3]) return;
				
				auto const chunkIndex{ chunk::MovingChunk{prevChunkChunk}.offseted(dir).getIndex() };
				assert(chunkIndex.is()); //chunk can't be invalid if it was edded to updatedBlocksNeighbours
				auto chunk{ chunks[chunkIndex.get()] };		
				auto &blocksData{ chunk.blocksData() };		
				auto &aabb{ chunk.aabb() };
				
				auto const aabbPart1{ aabb * Area{
					dir.applied([&](auto const axis, auto const i) { return                         0 + (axis > 0 ? 1 : 0); }),
					dir.applied([&](auto const axis, auto const i) { return units::blocksInChunkDim-1 - (axis < 0 ? 1 : 0); })
				} }; //drop 1 plane of blocks from aabb (or nothing if dir == 0)
				
				Area const updateArea{
					dir.applied([&](auto const axis, auto const i) { return (axis < 0 ? units::blocksInChunkDim-1 - 1 : 0); }),
					dir.applied([&](auto const axis, auto const i) { return (axis > 0 ? 1 : units::blocksInChunkDim-1); })
				}; //dropped part of aabb
				
				vec3i first{ units::blocksInChunkDim };
				vec3i last { -1 };
				
				iterateArea(updateArea.first, updateArea.last, [&](vec3i const blockCoord) {
					if(blocksData[pBlock{blockCoord}].isEmpty()) return;
					first = first.min(blockCoord);
					last  = last .max(blockCoord);
				});
				
				auto const aabbCombined{ aabbPart1 + Area{ first, last } };
				aabb = { aabbCombined.first, aabbCombined.last };
				
				chunk.status().setBlocksUpdated(true);
			});
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
			pCube const neighbourCubePos{ cubeCoord + vec3i{0, -1, 0} };
			auto const neighbourCubeChunkCoord{ neighbourCubePos.as<pos::Chunk>() };
			auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
			
			auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.offset(neighbourCubeChunkCoord.val()).get() };
			if(neighbourCubeChunkIndex == -1) return;
			
			auto neighbourCubeChunk{ chunks[neighbourCubeChunkIndex] };
			if(neighbourCubeChunk.data().cubeAt(neighbourCubeInChunkCoord).isSolid) return;
			
			auto &neighbourLiquidCube{ neighbourCubeChunk.liquid()[neighbourCubeInChunkCoord] };
			auto const toMax( chunk::LiquidCube::maxLevel - neighbourLiquidCube.level );
			
			if(toMax > 0 && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == id)) {
				auto diff{ std::min<int>(level, toMax) };
				//if(diff > maxLiquidToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; } //limit on downwards flow
				
				modified = true;
				level -= diff;		
				neighbourLiquidCube = { id, neighbourLiquidCube.level + diff };
				
				genNext.push_back({ neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) });
				blocksUpdatedInPrevChunk[neighbourCubePos.as<pBlock>()] = true;
				return;
			}
			
			return;
		}();
		
		//move liquid sideways
		if(level != 0) [&]() {
			struct LiquidCubeAndPos { 
				chunk::LiquidCube liquidCube; //copy
				chunk::LiquidCube *liquidCubeLoc; 
				
				chunk::ChunkAndCube posData;
				pCube cubeCoord;
				
				bool modified; 
				bool valid; 
			};
			
			static constexpr int sideNeighboursCount = 4;
			static constexpr vec3i dirs[sideNeighboursCount] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
			
			LiquidCubeAndPos cubes[sideNeighboursCount] = {};
			
			//fill the neighbours
			for(int i{}; i < sideNeighboursCount; i++) {
				auto const dir{ dirs[i] };
				
				pCube const neighbourCubePos{ cubeCoord + dir };
				auto const neighbourCubeChunkCoord{ neighbourCubePos.as<pos::Chunk>() };
				auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
				
				auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.offset(neighbourCubeChunkCoord.val()).get() };
				if(neighbourCubeChunkIndex == -1) continue;
				
				auto cubeNeighbourChunk{ chunks[neighbourCubeChunkIndex] };
				if(cubeNeighbourChunk.data().cubeAt(neighbourCubeInChunkCoord).isSolid) continue;
				
				auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[neighbourCubeInChunkCoord] };
				if((level > neighbourLiquidCube.level) && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == id)) {
					cubes[i] = { 
						neighbourLiquidCube, &neighbourLiquidCube, 
						{ neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) }, neighbourCubePos,
						false, true
					};
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
					blocksUpdatedInPrevChunk[cube.cubeCoord.as<pBlock>()] = true;
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