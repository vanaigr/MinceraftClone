#pragma once

#include"Position.h"
#include"Chunk.h"
#include"MiscChunk.h"

enum class LightingCubeType {
	medium, wall, emitter
};

namespace AddLighting {
	namespace {
		template<typename Config>
		inline void propagateAddLight(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord, uint8_t const startLight) {
			cubeChunk.status().current.lighting = false;
			iterateCubeNeighbours(
				cubeChunk, cubeInChunkCoord, 
				[startLight](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
					auto const cube{ cubeChunk.data().cubeAt2(pCube{cubeInChunkCoord}) };
					
					auto const type{ Config::getType(cube) };
					if(type == LightingCubeType::wall) return;
					
					auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
					if(type == LightingCubeType::medium) {
						auto const expectedLight{ Config::propagationRule(startLight, fromDir, cube) };
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
	inline void fromCube(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) {	
		auto const startLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
		propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, startLight);
	}
	
	//also causes first cube's neighbours to update their lighting
	template<typename Config>
	inline void fromCubeForcedFirst(chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) {
		auto const startLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
		iterateCubeNeighbours(
			cubeChunk, cubeInChunkCoord, 
			[&startLight](vec3i const fromDir, chunk::Chunk const cubeChunk, vec3i const cubeInChunkCoord) {
				auto const cube{ cubeChunk.data().cubeAt2(pCube{cubeInChunkCoord}) };	
				auto const type{ Config::getType(cube) };
				if(type == LightingCubeType::wall) return;
				
				auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
				if(type == LightingCubeType::medium) {
					auto const expectedLight{ Config::propagationRule(startLight, fromDir, cube) };
					if(cubeLight < expectedLight) cubeLight = expectedLight;
					propagateAddLight<Config>(cubeChunk, cubeInChunkCoord, cubeLight);
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
		template<typename Config>
		inline void propagateLightRemove(std::vector<chunk::ChunkAndCube> &endCubes, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord, uint8_t const cubeLight) {
			iterateCubeNeighbours(
				cubeChunk, cubeInChunkCoord,
				[&endCubes, fromLight = cubeLight](vec3i const fromDir, chunk::Chunk cubeChunk, vec3i const cubeInChunkCoord) -> void {
					auto const cube{ cubeChunk.data().cubeAt2(cubeInChunkCoord) };
					
					auto const type{ Config::getType(cube) };
					if(type == LightingCubeType::wall) return;
					
					auto &light{ Config::getLight(cubeChunk, cubeInChunkCoord) };
					auto const alreadyChecked{ light == 0 };
					auto const hasLight{ light != 0 };
					
					auto const expectedLight{ Config::propagationRule(fromLight, fromDir, cube) };
					if(type == LightingCubeType::medium && expectedLight >= light && !alreadyChecked) {
						cubeChunk.status().current.lighting = false;
						light = 0;
						propagateLightRemove<Config>(endCubes, cubeChunk, cubeInChunkCoord, expectedLight);
					}
					else if(hasLight) {
						endCubes.push_back(chunk::ChunkAndCube::fromCoord(cubeChunk.chunkIndex(), pCube{cubeInChunkCoord})); 
					}
				}
			);
		}
		
		template<typename Config>
		inline void removeLightInChunkCubes(
			chunk::Chunk cubesChunk, vec3i const cubesStartInChunkCoord, vec3i const cubesEndInChunkCoord, 
			std::vector<chunk::ChunkAndCube> &endCubes
		) {
			if((cubesEndInChunkCoord < cubesStartInChunkCoord).all()) return;
			
			cubesChunk.status().current.lighting = false;
			
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
						
						auto const cube{ cubeChunk.data().cubeAt2(pCube{cubeInChunkCoord}) };
						auto const type{ Config::getType(cube) };
						if(type == LightingCubeType::wall) return;
						
						auto &light{ Config::getLight(cubeChunk, cubeInChunkCoord) };
						if(type == LightingCubeType::medium) {
							auto const expectedLight{ light };
							cubeChunk.status().current.lighting = false;
							light = 0;
							propagateLightRemove<Config>(endCubes, cubeChunk, cubeInChunkCoord, expectedLight);
						}
						else {
							assert(type == LightingCubeType::emitter);
							endCubes.push_back(chunk::ChunkAndCube::fromCoord(cubeChunk.chunkIndex(), pCube{cubeInChunkCoord})); 
						}
					}
				);
			}
		}
	}
	
	template<typename Config>
	inline void inChunkCubes(chunk::Chunk chunk, vec3i const cubesStartInChunkCoord, vec3i const cubesEndInChunkCoord) {
		static std::vector<chunk::ChunkAndCube> endCubes{};
		endCubes.clear();
		
		auto &chunks{ chunk.chunks() };
	
		removeLightInChunkCubes<Config>(chunk, cubesStartInChunkCoord, cubesEndInChunkCoord, endCubes);
		
		for(auto const ci : endCubes) ::AddLighting::fromCube<Config>(chunks[ci.chunkIndex], ci.cubeCoord().val());
	}
}

