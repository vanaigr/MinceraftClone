#pragma once

#include"Chunk.h"
#include"Misc.h"
#include"Units.h"
#include"stdint.h"


inline bool useInAO(chunk::Block::id_t const id) {
	return id != 0 && id != 5 && id != 7 && id != 16;
}

inline bool useInCollision(chunk::Block::id_t const id) {
	return id != 0 && id != 5 && id != 16;
}

inline bool liquidThrough(chunk::Block::id_t const id) {
	return id == 0 || id == 5 || id == 16;
}

inline bool placeThrough(chunk::Block::id_t const id) {
	return id == 0 || id == 5|| id == 16;
}

inline int lightingLost(uint16_t const id) {
	     if(id == 0) return 0;
	else if(id == 5) return 3;
	else if(id == 7) return 2;
	else if(id == 16) return 0;
	//note: liquids are not used in lighting calculations
	else             return 0;
}

static constexpr int cubeLightingLosses = misc::divCeil<int>(chunk::ChunkLighting::maxValue, 32); /*
	lighting will at most propagate to neighbouring chunks (I hope)
*/

inline bool isBlockTranslucent(chunk::Block::id_t const id) {
	return id == 0 || id == 5 || id == 7 || id == 16; // && id != 15 //liquids are not checked anyway
}

inline bool isBlockEmitter(chunk::Block::id_t const id) {
	return id == 13 || id == 14;
}