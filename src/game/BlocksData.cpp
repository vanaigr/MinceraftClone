#include"BlocksData.h"
#include"Area.h"
#include"MiscChunk.h"

#include<chrono>
#include<tmmintrin.h> 
#include<type_traits>

#define USE_SIMD 1

#if USE_SIMD
void updateBlockDataWithoutNeighbours(chunk::Chunk const chunk, pBlock const blockCoord) {
	assert(chunk::checkBlockCoordInChunkValid(blockCoord));

	auto const &block{ chunk.blocks()[blockCoord] };
	auto const &liquid{ chunk.liquid() };
	auto const startingCubeOffset{ chunk::cubeCoordToIndex(blockCoord.as<pCube>()) };
	
	chunk::BlockData blockData{};
	blockData.solidCubes = block.cubes();
	
	static_assert(sizeof(chunk::Block::id_t) == 2);
	static_assert(sizeof(chunk::LiquidCube::level_t) == 1);
  #ifdef _MSC_VER
	//when SDL checks are enabled, pragma warning(disable:4700) doesn't disable the uninitialized variable error
	__m128i ids{ _mm_setzero_si128() };
	__m128i levels{ _mm_setzero_si128() };
  #else
	__m128i ids;
	__m128i levels;
  #endif	
	
	/*insert needs compile-time index, so the macro is used.
	  This same can be done with lambda, auto parameter and std::integral_constant*/
	#define set(INDEX) {\
		auto const &liquidCube{ liquid[startingCubeOffset + chunk::cubeCoordToIndex(chunk::cubeIndexInBlockToCoord(INDEX))] };\
		ids    = _mm_insert_epi16(ids, liquidCube.id, INDEX);\
		levels = _mm_insert_epi16(levels, liquidCube.level, INDEX);\
	}
	
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wuninitialized"
	
	static_assert(pos::cubesInBlockCount == 8);
	set(0)
	set(1)
	set(2)
	set(3)	
	set(4)
	set(5)
	set(6)
	set(7)
	
	#pragma clang diagnostic pop
	#undef set
	
	auto const areZ{ _mm_cmpeq_epi16(ids, _mm_setzero_si128()) };
	auto const areZ_{ _mm_shuffle_epi8(areZ, _mm_setr_epi8(0,2,4,6,8,10,12,14, 0,0,0,0,0,0,0,0)) }; //move 16 bit results for 8 bit movemask
	auto const isZ( _mm_movemask_epi8(areZ_) );
	blockData.liquidCubes = ~isZ;
		
	if(block.isEmpty() && isZ == 0/*all ids != 0*/) {
		auto const areAllSame{ _mm_cmpeq_epi16(ids, _mm_set1_epi16(_mm_cvtsi128_si32(ids))) };
		auto const areFull{ _mm_cmpeq_epi16(levels, _mm_set1_epi16(chunk::LiquidCube::maxLevel)) };
		auto const areFullSame{ _mm_and_si128(areAllSame, areFull) };
		/*no shuffling prior to movemask_epi8 because the result is just compared with 0
		  so the fact that individual cubes' results are duplicated in the mask is not important*/
		blockData.fullSameLiquid = ~uint16_t(_mm_movemask_epi8(areFullSame)) == 0;
	}

	chunk.blocksData()[blockCoord] = blockData;
}
#else
void updateBlockDataWithoutNeighbours(chunk::Chunk const chunk, pBlock const blockCoord) {
	assert(chunk::checkBlockCoordInChunkValid(blockCoord));

	auto const &block{ chunk.blocks()[blockCoord] };
	auto const &liquid{ chunk.liquid() };
	auto const blockLiquidId{ liquid[blockCoord.as<pCube>()].id }; //we can pick any cube in block
	
	chunk::BlockData blockData{};
	blockData.solidCubes = block.cubes();
	
	blockData.fullSameLiquid = block.isEmpty() && blockLiquidId != 0;
	chunk::LiquidCube const fullSameLiquidCube{ blockLiquidId, chunk::LiquidCube::maxLevel };	
	for(int i{}; i < pos::cubesInBlockCount; i++) {
		auto const cubeCoord{ blockCoord + chunk::cubeIndexInBlockToCoord(i) };
		auto const &liquidCube{ liquid[cubeCoord] };
		
		blockData.liquidCubes = blockData.liquidCubes | (decltype(blockData.liquidCubes)(liquidCube.id != 0) << i);
		blockData.fullSameLiquid &= liquidCube == fullSameLiquidCube;
	}

	chunk.blocksData()[blockCoord] = blockData;
}
#endif

void updateBlockDataNeighboursInfo(chunk::Chunk chunk, pBlock const blockCoord) {
	assert(chunk::checkBlockCoordInChunkValid(blockCoord));
	
	auto &chunks{ chunk.chunks() };
	auto &data{ chunk.blocksData() };
	auto const &liquid{ chunk.liquid() };
	
	auto &blockDataLoc{ data[blockCoord] };
	auto blockData{ blockDataLoc };
	
	auto const blockLiquidId{ liquid[blockCoord.as<pCube>()].id/*we can pick any cube in block*/ };

	auto const calcNoNeighbours{ blockData.noCubes() };
	auto const calcNeighboursFullSameLiquid{ bool(blockData.fullSameLiquid) };
	
	if(calcNoNeighbours || calcNeighboursFullSameLiquid) {
		blockData.noNeighbours             = calcNoNeighbours;
		blockData.neighboursFullSameLiquid = calcNeighboursFullSameLiquid;
		
		iterate3by3Volume([&](vec3i const neighbourDir, int const index) -> bool {
			if(neighbourDir == 0) return false;
			
			pBlock const neighbourBlockCoord{ blockCoord + neighbourDir };
			auto const neighbourBlockChunk  { neighbourBlockCoord.as<pChunk>() };
			auto const neighbourBlockInChunk{ neighbourBlockCoord.in<pChunk>() };
	
			auto chunkIndex{ chunk::MovingChunk{chunk}.offseted(neighbourBlockChunk.val()).getIndex() };
			if(!chunkIndex.is()) {
				blockData.neighboursFullSameLiquid = false;
				return false;
			}
			if(!blockData.noNeighbours && !blockData.neighboursFullSameLiquid) return true; //break
			
			auto const neighbourChunk{ chunks[chunkIndex.get()] };
			auto const &neighbourBlockData{ neighbourChunk.blocksData()[neighbourBlockInChunk] };
			
			if(blockData.noNeighbours) {
				blockData.noNeighbours &= neighbourBlockData.noCubes();
			}
			
			if(blockData.neighboursFullSameLiquid) {
				blockData.neighboursFullSameLiquid &= 
					neighbourBlockData.neighboursFullSameLiquid
					&& neighbourChunk.liquid()[neighbourBlockInChunk.as<pCube>()].id //we can pick any cube in block
					   == blockLiquidId;
			}
			
			return false;
		});
	}
	
	blockDataLoc = blockData; 
}

void updateBlocksDataWithoutNeighboursInArea(chunk::Chunk startChunk, pBlock const firstRel, pBlock const lastRel) {
	pChunk const startChunkPos{ startChunk.position() };
	
	auto const first{ firstRel + startChunkPos };
	auto const last { lastRel  + startChunkPos };
	
	iterateChunks(startChunk, first.as<pChunk>(), last.as<pChunk>(), [&](chunk::Chunk chunk, pChunk const chunkPos) {		
		auto const area{ intersectAreas3i(
			{ vec3i{0}, vec3i{units::blocksInChunkDim-1} }, 
			{ (first - chunkPos).valAs<pBlock>(), (last - chunkPos).valAs<pBlock>()  }
		) };
		
		if(area.isEmpty()) return;
		
		iterateArea(area.first, area.last, [&](pBlock const blockCoord) {
			updateBlockDataWithoutNeighbours(chunk, blockCoord);
		});
	});
}

void updateBlocksDataNeighboursInfoInArea(chunk::Chunk startChunk, pBlock const firstRel, pBlock const lastRel) {
	pChunk const startChunkPos{ startChunk.position() };
	
	auto const first{ firstRel + startChunkPos };
	auto const last { lastRel  + startChunkPos };
	
	iterateChunks(startChunk, first.as<pChunk>(), last.as<pChunk>(), [&](chunk::Chunk chunk, pChunk const chunkPos) {		
		auto const area{ intersectAreas3i(
			{ vec3i{0}, vec3i{units::blocksInChunkDim-1} }, 
			{ (first - chunkPos).valAs<pBlock>(), (last - chunkPos).valAs<pBlock>()  }
		) };
		
		if(area.isEmpty()) return;

		iterateArea(area.first, area.last, [&](pBlock const blockInChunkCoord) {
			updateBlockDataNeighboursInfo(chunk, blockInChunkCoord);
		});	
	});
}

void updateBlocksDataInArea(chunk::Chunk startChunk, pBlock const firstRel, pBlock const lastRel) {
	updateBlocksDataWithoutNeighboursInArea(startChunk, firstRel, lastRel);
	updateBlocksDataNeighboursInfoInArea   (startChunk, firstRel, lastRel);
}