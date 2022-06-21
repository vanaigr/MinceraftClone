#pragma once

#include"Position.h"
#include"Chunk.h"

enum class LightingCubeType {
	medium, wall, emitter
};

/*example struct Config {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk);
	
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord);
	
	static LightingCubeType getType(uint16_t const blockId, bool const cube);
	
	static uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube);
};*/
 
namespace AddLighting {
	namespace {
		template<typename Config>
		static void propagateAddLight(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord, uint8_t const startLight) {
			cubeChunk.status().setLightingUpdated(true);
			iterateCubeNeighbours(
				cubeChunk, cubeInChunkCoord, 
				[startLight](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
					auto const cube{ cubeChunk.data().cubeAt(cubeInChunkCoord) };
					auto const blockId{ cube.block.id() };
					auto const isCube{ cube.isSolid };
					
					auto const type{ Config::getType(blockId, isCube) };
					if(type == LightingCubeType::wall) return;
					
					auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
					if(type == LightingCubeType::medium) {
						auto const expectedLight{ Config::propagationRule(startLight, fromDir, blockId, isCube) };
						if(cubeLight < expectedLight) {
							cubeLight = expectedLight;
							propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, cubeLight);	
						}
					}
				}
			);
		}
	}
	
	template<typename Config>
	static void fromCube(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) {	
		auto const startLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
		propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, startLight);
	}
	
	template<typename Config>
	static void fromCubeForcedFirst(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) {
		auto const startLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
		iterateCubeNeighbours(
			cubeChunk, cubeInChunkCoord, 
			[&startLight](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
				auto const cube{ cubeChunk.data().cubeAt(cubeInChunkCoord) };
				auto const blockId{ cube.block.id() };
				auto const isCube{ cube.isSolid };
						
				auto const type{ Config::getType(blockId, isCube) };
				if(type == LightingCubeType::wall) return;
				
				auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
				if(type == LightingCubeType::medium) {
					auto const expectedLight{ Config::propagationRule(startLight, fromDir, blockId, isCube) };
					if(cubeLight < expectedLight) cubeLight = expectedLight;
					propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, cubeLight); //update even if starting cube's expected neighbour lighting is not enough
				}
				else {
					assert(type == LightingCubeType::emitter);
					propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, cubeLight); 
				}
			}
		);
	}
}
	
namespace SubtractLighting {
	namespace {
		struct CubeInfo { vec3i cubeCoord;/*could fit in 32 bits*/ int chunkIndex; };
		
		template<typename Config>
		static void propagateLightRemove(std::vector<CubeInfo> &endCubes, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord, uint8_t const cubeLight) {
			iterateCubeNeighbours(
				cubeChunk, cubeInChunkCoord,
				[&endCubes, fromLight = cubeLight](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
					auto const cube{ cubeChunk.data().cubeAt(cubeInChunkCoord) };
					auto const blockId{ cube.block.id() };
					auto const isCube{ cube.isSolid };
					
					auto const type{ Config::getType(blockId, isCube) };
					if(type == LightingCubeType::wall) return;
					
					auto &light{ Config::getLight(cubeChunk, cubeInChunkCoord) };
					auto const alreadyChecked{ light == 0 };
					auto const hasLight{ light != 0 };
					
					auto const expectedLight{ Config::propagationRule(fromLight, fromDir, blockId, isCube) };
					if(type == LightingCubeType::medium && expectedLight >= light && !alreadyChecked) {
						cubeChunk.status().setLightingUpdated(true);
						light = 0;
						propagateLightRemove<Config>(endCubes, cubeChunk, cubeInChunkCoord, expectedLight);
					}
					else if(hasLight) {
						endCubes.push_back(CubeInfo{cubeInChunkCoord, cubeChunk.chunkIndex()}); 
					}
				}
			);
		}
		
		template<typename Config>
		static void removeLightInChunkCubes(
			chunk::Chunk cubesChunk, vec3i const cubesStartInChunkCoord, vec3i const cubesEndInChunkCoord, 
			std::vector<CubeInfo> &endCubes
		) {
			if((cubesEndInChunkCoord < cubesStartInChunkCoord).all()) return;
			
			cubesChunk.status().setLightingUpdated(true);
			
			for(int32_t z{cubesStartInChunkCoord.z}; z <= cubesEndInChunkCoord.z; z++)
			for(int32_t y{cubesStartInChunkCoord.y}; y <= cubesEndInChunkCoord.y; y++)
			for(int32_t x{cubesStartInChunkCoord.x}; x <= cubesEndInChunkCoord.x; x++) {
				Config::getLight(cubesChunk, vec3i{x,y,z}) = 0u;
			}
			
			for(int32_t z{cubesStartInChunkCoord.z}; z <= cubesEndInChunkCoord.z; z++)
			for(int32_t y{cubesStartInChunkCoord.y}; y <= cubesEndInChunkCoord.y; y++)
			for(int32_t x{cubesStartInChunkCoord.x}; x <= cubesEndInChunkCoord.x; x++) {
				vec3i const cubeInChunkCoord{ x, y, z };
				
				iterateCubeNeighbours(
					cubesChunk, cubeInChunkCoord,
					[&endCubes, cubesChunk, cubesStartInChunkCoord, cubesEndInChunkCoord](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
						if(cubeChunk == cubesChunk && cubeInChunkCoord.in(cubesStartInChunkCoord, cubesEndInChunkCoord).all()) return;//skip cubes inside of starting area
						
						auto const cube{ cubeChunk.data().cubeAt(cubeInChunkCoord) };
						auto const blockId{ cube.block.id() };
						auto const isCube{ cube.isSolid };
						auto const type{ Config::getType(blockId, isCube) };
						if(type == LightingCubeType::wall) return;
						
						auto &light{ Config::getLight(cubeChunk, cubeInChunkCoord) };
						if(type == LightingCubeType::medium) {
							auto const expectedLight{ light };
							cubeChunk.status().setLightingUpdated(true);
							light = 0;
							propagateLightRemove<Config>(endCubes, cubeChunk, cubeInChunkCoord, expectedLight);
						}
						else {
							assert(type == LightingCubeType::emitter);
							endCubes.push_back(CubeInfo{cubeInChunkCoord, cubeChunk.chunkIndex()}); 
						}
					}
				);
			}
		}
	}
	
	template<typename Config>
	void inChunkCubes(chunk::Chunk chunk, vec3i const cubesStartInChunkCoord, vec3i const cubesEndInChunkCoord) {
		static std::vector<CubeInfo> endCubes{};
		endCubes.clear();
		
		auto &chunks{ chunk.chunks() };
	
		removeLightInChunkCubes<Config>(chunk, cubesStartInChunkCoord, cubesEndInChunkCoord, endCubes);
		
		for(auto const ci : endCubes) ::AddLighting::fromCube<Config>(chunks[ci.chunkIndex], ci.cubeCoord);
	}
}

