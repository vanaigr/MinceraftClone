#include"BlocksData.h"
#include"Area.h"
#include"MiscChunk.h"

void updateBlockDataWithoutNeighbours(chunk::Chunk chunk, pBlock const blockCoord) {
	assert(chunk::checkBlockCoordInChunkValid(blockCoord));
	
	auto &data{ chunk.blocksData() };
	auto const &blocks{ chunk.blocks() };
	auto const &liquid{ chunk.liquid() };
	
	chunk::Block::id_t blockLiquidId{ 0 };
	chunk::BlockData blockData{};
	
	auto const &block{ blocks[blockCoord] };
	
	blockData.solidCubes = block.cubes();
	blockData.fullSameLiquid = block.isEmpty();
	
	for(int i{}; i < pos::cubesInBlockCount; i++) {
		auto const cubeInBlockCoord{ chunk::cubeIndexInBlockToCoord(i) };
		auto const cubeCoord{ blockCoord + cubeInBlockCoord };
		
		auto const &liquidCube{ liquid[cubeCoord] };
		
		blockData.liquidCubes = blockData.liquidCubes | (decltype(blockData.liquidCubes)(liquidCube.id != 0) << i);
		
		if(liquidCube.id != 0 && liquidCube.level == chunk::LiquidCube::maxLevel && (blockLiquidId == 0 || liquidCube.id == blockLiquidId)) {
			blockLiquidId = liquidCube.id;
		}
		else blockData.fullSameLiquid = false;
	}
	
	data[blockCoord] = blockData;
}

void updateBlockDataNeighboursInfo(chunk::Chunk chunk, pBlock const blockCoord) {
	assert(chunk::checkBlockCoordInChunkValid(blockCoord));
	
	auto &chunks{ chunk.chunks() };
	auto &data{ chunk.blocksData() };
	auto const &liquid{ chunk.liquid() };
	
	auto &blockDataLoc{ data[blockCoord] };
	auto blockData{ blockDataLoc };
	
	chunk::Block::id_t blockLiquidId{
		liquid[blockCoord.as<pCube>()].id //we can pick any cube in block
	};

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