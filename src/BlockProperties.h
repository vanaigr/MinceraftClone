#pragma once

#include"Chunk.h"
#include"Misc.h"
#include"Units.h"
#include"stdint.h"

namespace Blocks {
	enum {
		airBlock = 0,
		grassBlock = 1,
		dirtBlock = 2,
		planksBlock = 3,
		woodBlock = 4,
		leavesBlock = 5,
		stoneBlock = 6,
		glassBlock = 7,
		diamondBlock = 8,
		obsidianBlock = 9,
		rainbowBlock = 10,
		firebrickBlock = 11,
		stoneBrickBlock = 12,
		lamp1Block = 13,
		lamp2Block = 14,
		water = 15,
		grass = 16,
	};
};


inline bool useInAO(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id != airBlock && id != leavesBlock && id != glassBlock && id != grass;
}

inline bool useInCollision(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id != airBlock && id != leavesBlock && id != grass;
}

inline bool liquidThrough(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id == airBlock || id == leavesBlock || id == grass;
}

inline bool placeThrough(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id == airBlock || id == leavesBlock || id == grass;
}

inline int lightingLost(chunk::Block::id_t const id) {
	using namespace Blocks;
	     if(id == airBlock) return 0;
	else if(id == leavesBlock) return 3;
	else if(id == glassBlock) return 2;
	else if(id == grass) return 0;
	//note: liquids are not used in lighting calculations
	else             return 0;
}

static constexpr int cubeLightingLosses = misc::divCeil<int>(chunk::ChunkLighting::maxValue, 32); /*
	lighting will at most propagate to neighbouring chunks (I hope)
*/

inline bool isBlockTranslucent(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id == airBlock || id == leavesBlock || id == glassBlock || id == grass;
}

inline bool isBlockEmitter(chunk::Block::id_t const id) {
	using namespace Blocks;
	return id == lamp1Block || id == lamp2Block;
}