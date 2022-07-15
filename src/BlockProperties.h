#pragma once

#include"Chunk.h"
#include"Misc.h"
#include"Units.h"
#include"stdint.h"

inline bool useInAO(chunk::ChunkData::Cube const cube) {
	auto const id{ cube.block.id() * cube.isSolid };
	
	return id != 0 && id != 5 && id != 7 && id != 16;
}

inline bool useInCollision(chunk::Block::id_t const id) {
	return id != 0 && id != 5 && id != 16;
}

inline int lightingLost(uint16_t const id) {
	     if(id == 0) return 0;
	else if(id == 5) return 3;
	else if(id == 7) return 2;
	else if(id == 16) return 0;
	else             return 0;
}

static constexpr int cubeLightingLosses = misc::divCeil<int>(chunk::ChunkLighting::maxValue, 32); /*
	lighting will at most propagate to neighbouring chunks (I hope)
*/

inline bool isBlockTranslucent(uint16_t const id) {
	return id == 0 || id == 5 || id == 7 || id == 16;
}

inline bool isBlockEmitter(uint16_t const id) {
	return id == 13 || id == 14;
}