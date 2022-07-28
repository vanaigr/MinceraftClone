#include"Area.h"
#include"MiscChunk.h"
#include"BlocksData.h"
#include"BlockProperties.h"

#include<iostream>
#include<unordered_set>
#include<functional>
#include<stdint.h>
#include<type_traits>
#include<algorithm>
#include<array>
#include<memory>

#include<chrono>

struct ChunkAndCubeHash { 
	inline size_t operator()(chunk::ChunkAndCube const it) const noexcept {
		static_assert(sizeof(it.chunkIndex) == 4);
		static_assert(sizeof(it.cubeIndex) == 2);
		return size_t(it.chunkIndex) | (size_t(it.cubeIndex) << 32);
	} 
};

static constexpr chunk::LiquidCube::level_t maxLiquidToNeighbour{ chunk::LiquidCube::maxLevel/*effectivelly no limit*/ };

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

static int nmoveDown = 4;
void chunk::ChunksLiquidCubes::update() {
	nmoveDown--;
	if(nmoveDown == -1) nmoveDown = 4;
	
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
			
			//update AABB, status, modified
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
				chunk.modified() = true;
			});
		}
		
		prevChunk = {};
		blocksUpdatedInPrevChunk.reset();
	};
	
	for(size_t i{}, size{ gen.size() }; i < size; i++) {
		auto const cubePosData{ gen[i] };
		
		if(chunk::OptionalChunkIndex{cubePosData.chunkIndex} != prevChunk) {
			updatePrevChunkData();
		}

		auto chunk{ chunks[cubePosData.chunkIndex] };
		auto const cubeCoord{ chunk::cubeIndexToCoord(cubePosData.cubeIndex) };
	
		auto &liquidCubeLoc{ chunk.liquid()[cubePosData.cubeIndex] };
		
		{ //move liquid to neighbours
			auto const liquidCube{ liquidCubeLoc };
			
			if(liquidCube.liquid()) {
				auto level{ liquidCube.level };
				auto falling{ liquidCube.falling };
				auto keepUpdating{ false };
				auto moveSideways{ true };
				
				//move liquid downwards
				if(level != 0) [&]() {
					pCube const neighbourCubePos{ cubeCoord + vec3i{0, -1, 0} };
					auto const neighbourCubeChunkCoord{ neighbourCubePos.as<pos::Chunk>() };
					auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
					
					auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.offset(neighbourCubeChunkCoord.val()).get() };
					if(neighbourCubeChunkIndex == -1) { falling = false; return; }
					
					auto neighbourCubeChunk{ chunks[neighbourCubeChunkIndex] };
					if(!liquidThrough(neighbourCubeChunk.data().cubeAt2(neighbourCubeInChunkCoord))) { falling = false; return; }
					
					auto &neighbourLiquidCube{ neighbourCubeChunk.liquid()[neighbourCubeInChunkCoord] };
					
					auto const toMax( chunk::LiquidCube::maxLevel - neighbourLiquidCube.level );
					auto const canMove{ toMax > 0 && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == liquidCube.id) };
					
					if(neighbourLiquidCube.liquid() && canMove) {
						moveSideways = false;
						
						if(nmoveDown) {
							keepUpdating = true;
							return;
						}
						
						auto diff{ std::min<int>(level, toMax) };
						//if(diff > maxLiquidToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; } //limit on downward flow
						
						level -= diff;		
						neighbourLiquidCube = LiquidCube::liquid(liquidCube.id, neighbourLiquidCube.level + diff, true);
						
						genNext.push_back({ neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) });
						blocksUpdatedInPrevChunk[neighbourCubePos.as<pBlock>()] = true;
						return;
					}
					else if(neighbourLiquidCube.outflow && canMove) {
						moveSideways = false;
						
						if(nmoveDown) {
							keepUpdating = true;
							return;
						}
						
						auto diff{ std::min<int>(level, toMax) };
						//if(diff > maxLiquidToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; } //limit on downward flow
						
						level -= diff;
						keepUpdating |= level != 0;
						return;
					}
					else falling = false;
					
					return;
				}();
				
				//move liquid sideways	
				if(moveSideways && level != 0) [&]() {
					struct LiquidCubeLevel {
						chunk::LiquidCube::level_t level;
						bool update;
					};
					struct LiquidCubeAndPos { 
						chunk::LiquidCube *liquidCubeLoc; 
						chunk::LiquidCube liquidCube; //copy
						
						chunk::ChunkAndCube posData;
						pCube cubeCoord;
						bool valid;
					};
					
					static constexpr int sideNeighboursCount = 4;
					static constexpr vec3i dirs[sideNeighboursCount] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
					
					LiquidCubeLevel liquidCubes[sideNeighboursCount] = {};
					LiquidCubeAndPos cubesInfo[sideNeighboursCount] = {};
					
					//fill the neighbours
					for(int i{}; i < sideNeighboursCount; i++) {
						auto const dir{ dirs[i] };
						
						pCube const neighbourCubePos{ cubeCoord + dir };
						auto const neighbourCubeChunkCoord{ neighbourCubePos.as<pos::Chunk>() };
						auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
						
						auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.offset(neighbourCubeChunkCoord.val()).get() };
						if(neighbourCubeChunkIndex == -1) continue;
						
						auto cubeNeighbourChunk{ chunks[neighbourCubeChunkIndex] };
						if(!liquidThrough(cubeNeighbourChunk.data().cubeAt2(neighbourCubeInChunkCoord))) continue;
						
						auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[neighbourCubeInChunkCoord] };
						auto const canMove{ level > neighbourLiquidCube.level && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == liquidCube.id) };
						if((neighbourLiquidCube.liquid() || neighbourLiquidCube.outflow) && canMove) {
							liquidCubes[i] = {
								neighbourLiquidCube.level,
								true
							};
							cubesInfo[i] = { 
								&neighbourLiquidCube, neighbourLiquidCube,
								{ neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) }, neighbourCubePos,
								true
							};
						}
					}
					
					//average out liquid levels between current cube and its neighbours
					while(true) {
						int validCubesCount{};
						int liquidTotal{ level };
						for(int i{}; i < sideNeighboursCount; i++) {
							auto const &neighbourCube{ liquidCubes[i] };
							if(!neighbourCube.update) continue;
							validCubesCount++;
							liquidTotal += neighbourCube.level;
						}
						if(validCubesCount == 0) break;
						
						auto const avgLevel{ liquidTotal / (validCubesCount+1) }; //this variant leaves MORE liquid in current cube
						int movedToNeighbours{ 0 };
						
						for(int i{}; i < sideNeighboursCount; i++) {
							auto &neighbourCube{ liquidCubes[i] };
							if(!neighbourCube.update) continue;
							
							auto diff{ avgLevel - neighbourCube.level };
							auto const moreToNeighbour{ diff > maxLiquidToNeighbour };
							if(moreToNeighbour) { diff = maxLiquidToNeighbour; keepUpdating = true; }
							
							if(diff > 0) {
								movedToNeighbours += diff;
								neighbourCube.level += diff;
							}
							
							if(diff <= 0 || moreToNeighbour) neighbourCube.update = false;
						}
						
						if(movedToNeighbours != 0) {
							level -= movedToNeighbours;
						}
					}
					
					//write modified cubes
					for(int i{}; i < sideNeighboursCount; i++) {
						auto &neighbourCube{ liquidCubes[i] };
						auto &neighbourCubeInfo{ cubesInfo[i] };
						if(!neighbourCubeInfo.valid) continue;
						
						if(neighbourCubeInfo.liquidCube.liquid()) {
							auto const newCube{ chunk::LiquidCube::liquid(liquidCube.id, neighbourCube.level, neighbourCubeInfo.liquidCube.falling) };
							
							if(neighbourCubeInfo.liquidCube != newCube) {
								*neighbourCubeInfo.liquidCubeLoc = newCube;
								genNext.push_back(neighbourCubeInfo.posData);
								blocksUpdatedInPrevChunk[neighbourCubeInfo.cubeCoord.as<pBlock>()] = true;
							}
						}
						else if(neighbourCubeInfo.liquidCube.outflow)/*
							we don't need to do anything regarding `keepUpdating` because it 
							had been set by the averaging algorithm above if needed
						*/;
						else assert(false); //inflow cubes should not be there
					}
				}();
				
				//write modified cube
				auto const newLiquidCube{ chunk::LiquidCube::liquid(liquidCube.id, level, nmoveDown ? liquidCube.falling : falling) };
				if(liquidCube != newLiquidCube) {
					liquidCubeLoc = newLiquidCube;
					prevChunk = cubePosData.chunkIndex;
					blocksUpdatedInPrevChunk[cubeCoord.as<pBlock>()] = true;
				}
				if(keepUpdating || nmoveDown) {
					genNext.push_back(cubePosData);
				}
			}
			else if(liquidCube.inflow) {
				static vec3i const neighbourDirs[]{
					{-1,0,0}, {1,0,0},
					{0,-1,0},// {0,1,0}, upward direction is not used
					{0,0,-1}, {0,0,1},
				};
				
				for(auto const dir : neighbourDirs) {
					pCube const neighbourCubePos{ cubeCoord + dir };
					auto const neighbourCubeChunkCoord{ neighbourCubePos.as<pos::Chunk>() };
					auto const neighbourCubeInChunkCoord{ neighbourCubePos.in<pos::Chunk>() };
					
					auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.offset(neighbourCubeChunkCoord.val()).get() };
					if(neighbourCubeChunkIndex == -1) continue;
					
					auto neighbourCubeChunk{ chunks[neighbourCubeChunkIndex] };
					if(!liquidThrough(neighbourCubeChunk.data().cubeAt2(neighbourCubeInChunkCoord))) continue;
					
					auto &neighbourLiquidCube{ neighbourCubeChunk.liquid()[neighbourCubeInChunkCoord] };
					
					auto const toMax( chunk::LiquidCube::maxLevel - neighbourLiquidCube.level );
					auto const canMove{ toMax > 0 && (neighbourLiquidCube.id == 0 || neighbourLiquidCube.id == liquidCube.id) };
					
					if(neighbourLiquidCube.liquid() && canMove) {					
						auto diff{ std::min<int>(liquidCube.level, toMax) };		
						neighbourLiquidCube = LiquidCube::liquid(liquidCube.id, neighbourLiquidCube.level + diff, false);
						
						genNext.push_back({ neighbourCubeChunkIndex, chunk::cubeCoordToIndex(neighbourCubeInChunkCoord) });
						blocksUpdatedInPrevChunk[neighbourCubePos.as<pBlock>()] = true;
						continue;
					}
					else /* other types of liquid cubes like inflows and outflows are ignored */;
				}
				
				genNext.push_back(cubePosData);
			}
		}
	
		{ //mark cubes that could move liquid to this cube as needing pudate
			auto const liquidCube{ liquidCubeLoc };
			
			if(liquidCube.liquid() || liquidCube.outflow) {
				if(liquidCube.level < chunk::LiquidCube::maxLevel) {
					auto const fromAbove = [&]() {
						auto const neighbourCubePosData = getNeighbourCube(chunk, cubeCoord, vec3i{0, 1, 0});
						if(neighbourCubePosData.chunkIndex == -1) return false;
						
						auto cubeUpChunk{ chunks[neighbourCubePosData.chunkIndex] };
						auto const cubeUpCoord{ chunk::cubeIndexToCoord(neighbourCubePosData.cubeIndex) };
						if(!liquidThrough(cubeUpChunk.data().cubeAt2(cubeUpCoord))) return false ;
						
						auto &upLiquidCube{ cubeUpChunk.liquid()[neighbourCubePosData.cubeIndex] };
						if(upLiquidCube.liquid() && upLiquidCube.level > 0 && upLiquidCube.id == liquidCube.id) {
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
							if(neighbourCubePosData.chunkIndex == -1) continue;
							
							auto cubeNeighbourChunk{ chunks[neighbourCubePosData.chunkIndex] };
							auto const cubeNeighbourCoord{ chunk::cubeIndexToCoord(neighbourCubePosData.cubeIndex) };
							if(!liquidThrough(cubeNeighbourChunk.data().cubeAt2(cubeNeighbourCoord))) continue;
							
							auto &neighbourLiquidCube{ cubeNeighbourChunk.liquid()[neighbourCubePosData.cubeIndex] };
							if(neighbourLiquidCube.level > liquidCube.level && neighbourLiquidCube.id == liquidCube.id) {
								genNext.push_back(neighbourCubePosData);
							}
						}
					}
				}
			}
		}
	}
	
	updatePrevChunkData();
	
	genIndex = (genIndex+1) % gensCount;
}