#pragma once

#include"LightingPropagation.h"
#include"BlockProperties.h"
#include"Area.h"

struct ConfigInstance { //type erased config I guess
private:
	template<typename Type> struct T {}; //is used to pass template arguments to the constructor
public:
	template<typename Config>
	static constexpr ConfigInstance from() {
		return ConfigInstance{ T<Config>{} };
	}
	
private:
	chunk::ChunkLighting& (  *getLighting_      )(chunk::Chunk const chunk);
	uint8_t&              (  *getLight_         )(chunk::Chunk const chunk, vec3i const cubeInChunkCoord);
	LightingCubeType      (  *getType_          )(chunk::Block::id_t const id);
	uint8_t               (  *propagationRule_  )(chunk::ChunkLighting::value_type const lighting, vec3i const fromDir, chunk::Block::id_t const toBlockId);

	template<typename Config, template <typename> typename T>
	constexpr ConfigInstance(T<Config>) :
		getLighting_{ &Config::getLighting },
		getLight_{ &Config::getLight },
		getType_{ &Config::getType },
		propagationRule_{ &Config::propagationRule }
	{}
public:
	chunk::ChunkLighting &getLighting(chunk::Chunk chunk) const { return getLighting_(chunk); }
	
	uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) const { return getLight_(chunk, cubeInChunkCoord); }
	
	LightingCubeType getType(chunk::Block::id_t const id) const { return getType_(id); }
	
	uint8_t propagationRule(chunk::ChunkLighting::value_type const lighting, vec3i const fromDir, chunk::Block::id_t const toBlockId) const { 
		return propagationRule_(lighting, fromDir, toBlockId); 
	}
};


struct SkyLightingConfig {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk) {
		return chunk.skyLighting();
	}
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) { 
		return getLighting(chunk)[cubeInChunkCoord]; 
	}
	
	static LightingCubeType getType(chunk::Block::id_t const id) { 
		if(isBlockTranslucent(id)) return LightingCubeType::medium;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(chunk::ChunkLighting::value_type const lighting, vec3i const fromDir, chunk::Block::id_t const toBlockId) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		return misc::max(
			int(lighting)
			 - (fromDir == vec3i{0,-1,0} ? 0 : cubeLightingLosses)
			 - lightingLost(toBlockId), 
			int(0)
		);
	}
};

struct BlocksLightingConfig {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk) {
		return chunk.blockLighting();
	}
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) { 
		return getLighting(chunk)[cubeInChunkCoord]; 
	}
	
	static LightingCubeType getType(chunk::Block::id_t const blockId) { 
		if(isBlockTranslucent(blockId)) return LightingCubeType::medium;
		else if(isBlockEmitter(blockId)) return LightingCubeType::emitter;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(chunk::ChunkLighting::value_type const lighting, vec3i const fromDir, chunk::Block::id_t const toBlockId) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		return misc::max(
			int(lighting) - cubeLightingLosses - lightingLost(toBlockId), 
			int(0)
		);
	}
};

void calculateLighting(chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], vec2i const columnPosition, int const lowestEmptyY);