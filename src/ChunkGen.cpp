#include"ChunkGen.h"
#include"BlockProperties.h"
#include"Lighting.h"
#include"BlocksData.h"
#include"AO.h"

#include"PerlinNoise.h"

#include<string>
#include<sstream>
#include<fstream>

static std::string chunkFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save/" << pos << ".cnk";
	return ss.str();
}

static std::string chunkNewFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save2/" << pos << ".cnk2";
	return ss.str();
}

static std::string chunkColumnFilename(vec2i const columnChunkCoordXY) {
	std::stringstream ss{};
	ss << "./save/" << columnChunkCoordXY << ".ccm";
	return ss.str();
}

void writeChunk(chunk::Chunk chunk) {
	std::cout << "!";
	auto const &data{ chunk.data() };
	
	std::ofstream chunkFileOut{ chunkNewFilename(chunk), std::ios::binary };
	
	for(int x{}; x < units::blocksInChunkDim; x++) 
	for(int y{}; y < units::blocksInChunkDim; y++) 
	for(int z{}; z < units::blocksInChunkDim; z++) {
		vec3i const blockCoord{x,y,z};
		auto const blockData = data[chunk::blockIndex(blockCoord)].data();
		
		uint8_t const blk[] = { 
			(unsigned char)((blockData >> 0) & 0xff), 
			(unsigned char)((blockData >> 8) & 0xff),
			(unsigned char)((blockData >> 16) & 0xff),
			(unsigned char)((blockData >> 24) & 0xff),
		};
		chunkFileOut.write(reinterpret_cast<char const *>(&blk[0]), 4);
	}
}

bool tryReadChunk(chunk::Chunk chunk) {
	auto &data{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	
	auto &aabbLoc{ chunk.aabb() };
	auto aabb{ aabbLoc };
	
	auto const filename2{ chunkNewFilename(chunk) };
	std::ifstream chunkFileIn2{ filename2, std::ios::binary };	
	if(!chunkFileIn2.fail()) {
		for(int x{}; x < units::blocksInChunkDim; x++) 
		for(int y{}; y < units::blocksInChunkDim; y++) 
		for(int z{}; z < units::blocksInChunkDim; z++) 
		{
			vec3i const blockCoord{x,y,z};
			//uint16_t &block = data[chunk::blockIndex(blockCoord)];
			uint8_t blk[4];
			
			chunkFileIn2.read( reinterpret_cast<char *>(&blk[0]), 4 );
			
			uint32_t const block( 
				  (uint32_t(blk[0]) << 0 )
				| (uint32_t(blk[1]) << 8 )
				| (uint32_t(blk[2]) << 16)
				| (uint32_t(blk[3]) << 24)					
			);
			
			data[chunk::blockIndex(blockCoord)] = chunk::Block(block);
			if(block != 0) aabb += { blockCoord };
			if(isBlockEmitter(block)) emitters.add(blockCoord);
		}

		aabbLoc = aabb;
		return true;
	}
	chunkFileIn2.close();
	
	auto const filename{ chunkFilename(chunk) };
	std::ifstream chunkFileIn{ filename, std::ios::binary };
	if(!chunkFileIn.fail()) {
		for(int x{}; x < units::blocksInChunkDim; x++) 
		for(int y{}; y < units::blocksInChunkDim; y++) 
		for(int z{}; z < units::blocksInChunkDim; z++) {
			vec3i const blockCoord{x,y,z};
			//uint16_t &block = data[chunk::blockIndex(blockCoord)];
			uint8_t blk[2];
			
			chunkFileIn.read( reinterpret_cast<char *>(&blk[0]), 2 );
			
			uint16_t const block( blk[0] | (uint16_t(blk[1]) << 8) );
			
			data[chunk::blockIndex(blockCoord)] = chunk::Block::fullBlock(block);
			if(block != 0) aabb += { blockCoord };
			if(isBlockEmitter(block)) emitters.add(blockCoord);
		}
		
		aabbLoc = aabb;
		return true;
	}
	
	return false;
}

static double heightAt(vec2i const flatChunk, vec2i const block) {
	static siv::PerlinNoise perlin{ (uint32_t)rand() };
	auto const value = perlin.octave2D(
		(flatChunk.x * 1.0 * units::blocksInChunkDim + block.x) / 20.0, 
		(flatChunk.y * 1.0 * units::blocksInChunkDim + block.y) / 20.0, 
		3
	);
						
	return misc::map<double>(misc::clamp<double>(value,-1,1), -1, 1, 5, 15);
}

static vec3i getTreeBlock(vec2i const flatChunk) {
	static auto const random = [](vec2i const co) -> double {
		auto const fract = [](auto it) -> auto { return it - std::floor(it); };
		return fract(sin( vec2d(co).dot(vec2d(12.9898, 78.233)) ) * 43758.5453);
	};
	
	vec2i const it( 
		(vec2d(random(vec2i{flatChunk.x+1, flatChunk.y}), random(vec2i{flatChunk.x*3, flatChunk.y*5})) 
		 * units::blocksInChunkDim)
		.floor().clamp(0, units::blocksInChunkDim)
	);
	
	auto const height{ heightAt(flatChunk,it) };
	
	return vec3i{ it.x, int32_t(std::floor(height))+1, it.y };
}

static void genTrees(chunk::Chunk chunk) {	
	auto const &chunkCoord{ chunk.position() };
	auto &data{ chunk.data() };
	auto &emitters{ chunk.emitters() };
	
	auto &aabbLoc{ chunk.aabb() };
	auto aabb{ aabbLoc };
	
	for(int32_t cx{-1}; cx <= 1; cx ++) 
	for(int32_t cz{-1}; cz <= 1; cz ++) {
		vec3i const chunkOffset{ cx, -chunkCoord.y, cz };
		auto const curChunk{ chunkCoord + chunkOffset };
		
		auto const treeBlock{ getTreeBlock(vec2i{curChunk.x, curChunk.z}) };
		
		for(int32_t x{-2}; x <= 2; x++) 
		for(int32_t y{0} ; y < 6 ; y++) 
		for(int32_t z{-2}; z <= 2; z++) {
			vec3i tl{ x,y,z };// tree-local block
			auto const blk{ chunkOffset * units::blocksInChunkDim + treeBlock + tl };
			
			if(blk.inMMX(vec3i{0}, vec3i{units::blocksInChunkDim}).all()) {
				auto const index{ chunk::blockIndex(blk) };
				chunk::Block &curBlock{ data[index] };
				
				if(curBlock.id() == 0) {
					bool is = false;
					if((is = tl.x == 0 && tl.z == 0 && tl.y <= 4)) curBlock = chunk::Block::fullBlock(4);
					else if((is = 
							(tl.y >= 2 && tl.y <= 3
							&& !( (abs(x) == abs(z))&&(abs(x)==2) )
							) || 
							(tl.in(vec3i{-1, 4, -1}, vec3i{1, 5, 1}).all()
							&& !( (abs(x) == abs(z))&&(abs(x)==1) &&(tl.y==5 || (treeBlock.x*(x+1)/2+treeBlock.z*(z+1)/2)%2==0) )
							)
					)) curBlock = chunk::Block::fullBlock(5);
					
					if(is) aabb += {blk};
					if(isBlockEmitter(curBlock.id())) emitters.add(blk);
				}
			}
		}
	}
	
	aabbLoc = aabb;
}

static void genChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk) {
	auto const &pos{ chunk.position() };
	auto &blocks{ chunk.data() };
	auto &liquid{ chunk.liquid() };
	auto &emitters{ chunk.emitters() };
	
	auto &aabbLoc{ chunk.aabb() };
	auto aabb{ aabbLoc };
	
	for(int z{}; z < units::blocksInChunkDim; ++z)
	for(int y{}; y < units::blocksInChunkDim; ++y) 
	for(int x{}; x < units::blocksInChunkDim; ++x) {
		vec3i const blockCoord{ x, y, z };
		
		auto const height{ heights[z * units::blocksInChunkDim + x] };
		
		//if(misc::mod(int32_t(height), 9) == misc::mod((pos.y * units::blocksInChunkDim + y + 1), 9)) { //repeated floor
		double const diff{ height - double(pos.y * units::blocksInChunkDim + y) };
		if(diff >= 0) {
			uint16_t block;
			
			if(diff < 1) block = 1; //grass
			else if(diff < 5) block = 2; //dirt
			else block = 6; //stone
			
			blocks[blockCoord] = chunk::Block::fullBlock(block);
			aabb += {blockCoord};
		}
		else {
			blocks[blockCoord] = chunk::Block::emptyBlock();
			if(pos.y * units::blocksInChunkDim + y == 7) {
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					pCube const cubeLocalCoord{ chunk::Block::cubeIndexPos(cubeIndex) };
					auto const cubeCoord{ pBlock{blockCoord} + cubeLocalCoord }; 
					
					if(cubeLocalCoord.val().y == units::cubesInBlockDim-1)
						liquid[cubeCoord] = chunk::LiquidCube{15, 254u};
					else 
						liquid[cubeCoord] = chunk::LiquidCube{15, 255u};
				}
			}
			if(pos.y * units::blocksInChunkDim + y < 7) {
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					auto const cubeCoord{ pBlock{blockCoord} + pCube{ chunk::Block::cubeIndexPos(cubeIndex) } }; 
					liquid[cubeCoord] = chunk::LiquidCube{15, 255u};
				}
			}
		}
		
		
		if(isBlockEmitter(blocks[blockCoord].id())) emitters.add(blockCoord);
	}
	
	aabbLoc = aabb;
	genTrees(chunk);	
}

static void fillChunkData(double const (&heights)[units::blocksInChunkDim * units::blocksInChunkDim], chunk::Chunk chunk, bool const loadChunks) {
	if(loadChunks  && tryReadChunk(         chunk)) chunk.modified() = false;
	else {            genChunkData(heights, chunk); chunk.modified() = true ; }
	
}

void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition, bool const loadChunks) {
	//generate heights for each block column in the chunk column
	double heights[units::blocksInChunkDim * units::blocksInChunkDim];
	auto minHeight{ std::numeric_limits<double>::infinity() };
	for(int z{}; z < units::blocksInChunkDim; z++) 
	for(int x{}; x < units::blocksInChunkDim; x++) {
		auto const height{  heightAt(vec2i{columnPosition.x,columnPosition.y}, vec2i{x,z}) };
		heights[z* units::blocksInChunkDim + x] = height;
		minHeight = std::min(minHeight, height);
	}

	constexpr int neighbourDirsCount = 8; //horizontal neighbours only
	vec3i const neighbourDirs[] = { 
		vec3i{-1,0,-1}, vec3i{-1,0,0}, vec3i{-1,0,+1}, vec3i{0,0,+1}, vec3i{+1,0,+1}, vec3i{+1,0,0}, vec3i{+1,0,-1}, vec3i{0,0,-1}
	};
	chunk::Move_to_neighbour_Chunk neighbourChunks[neighbourDirsCount] = { 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}, 
		{chunks}
	};
	neighbourChunks[0] = {chunks, vec3i{columnPosition.x, chunkColumnChunkYMax, columnPosition.y} + neighbourDirs[0]};
	
	for(int i{1}; i < neighbourDirsCount; i++) {
		auto mtn{ chunk::Move_to_neighbour_Chunk{neighbourChunks[i-1]} };
		mtn.move(vec3i{columnPosition.x, chunkColumnChunkYMax, columnPosition.y} + neighbourDirs[i]);
		neighbourChunks[i] = mtn;
	}
	
	chunk::OptionalChunkIndex topNeighbourIndex{};
	
	int chunkIndices[chunksCoumnChunksCount];
	
	auto lowestEmptyY{ chunkColumnChunkYMax + 1 };
	auto lowestNotFullY  { chunkColumnChunkYMax + 1 };
	auto emptyBefore{ true };

	auto lowestWithBlockLighting { chunkColumnChunkYMax + 1 };
	auto highestWithBlockLighting{ chunkColumnChunkYMin - 1 };
	
	struct ChunkIndexAndNeighbours{ 
		int chunkIndex; 
		chunk::OptionalChunkIndex neighbours[neighbourDirsCount];
	};
	ChunkIndexAndNeighbours chunkIndicesWithNeighbours[chunksCoumnChunksCount];
	
	for(auto y { chunkColumnChunkYMax }; y >= chunkColumnChunkYMin; y--) {
		auto const usedIndex{ chunks.reserve() };
		
		auto  const chunkIndex{ chunks.usedChunks()[usedIndex] };
		vec3i const chunkPosition{ columnPosition.x, y, columnPosition.y };
		
		chunks.chunksIndex_position[chunkPosition] = chunkIndex;
		chunkIndices[y - chunkColumnChunkYMin] = chunkIndex;
		
		auto chunk{ chunks[chunkIndex] };
		
		chunk.position() = chunkPosition;
		chunk.status() = chunk::ChunkStatus{};
		chunk.status().setEverythingUpdated();
		chunk.status().setUpdateAO(true);
		chunk.status().setUpdateNeighbouringEmitters(true);
		chunk.gpuIndex() = chunk::Chunks::index_t{};
		chunk.liquid().reset();
		chunk.ao().reset();
		chunk.blockLighting().reset();
		chunk.emitters().clear();
		chunk.neighbouringEmitters().clear();
		
		chunk.neighbours() = [&] {
			chunk::Neighbours neighbours{};
			
			for(int j{}; j < neighbourDirsCount; j++) {
				vec3i const offset{ neighbourDirs[j] };
				if(!chunk::Neighbours::checkDirValid(offset)) continue;
					
				auto const neighbourIndex{ neighbourChunks[j].optChunk().get() };
				
				if(neighbourIndex >= 0) {
					auto neighbourChunk{ chunks[neighbourIndex] };
					neighbours[offset] = chunk::OptionalChunkIndex(neighbourIndex);
					neighbourChunk.neighbours()[chunk::Neighbours::mirror(offset)] = chunkIndex;
				}
				else neighbours[offset] = chunk::OptionalChunkIndex();
			}
			
			{
				vec3i const offset{ 0, 1, 0 };
				auto const neighbourIndex{ topNeighbourIndex.get() };
				
				if(neighbourIndex >= 0) {
					neighbours[offset] = chunk::OptionalChunkIndex(neighbourIndex);
					chunks[neighbourIndex].neighbours()[chunk::Neighbours::mirror(offset)] = chunkIndex;
				}
				else neighbours[offset] = chunk::OptionalChunkIndex();
			}
			
			return neighbours;
		}();
		
		chunk.aabb() = Area::empty();
		
		fillChunkData(heights, chunk, loadChunks);
		fillEmittersBlockLighting(chunk);
		
		if(emptyBefore && chunk.aabb().isEmpty()) {
			chunk.skyLighting().fill(chunk::ChunkLighting::maxValue);
			lowestEmptyY = misc::min(lowestEmptyY, chunkPosition.y);
		}
		else {
			chunk.skyLighting().reset();
			emptyBefore = false;
		}
		if(chunk.emitters().size() != 0) {
			highestWithBlockLighting = std::max(highestWithBlockLighting, chunkPosition.y);
			lowestWithBlockLighting  = std::min(lowestWithBlockLighting , chunkPosition.y);
		}
		
		if((y + 1)*units::blocksInChunkDim-1 >= minHeight) lowestNotFullY = std::min(lowestNotFullY, y);
		
		chunkIndicesWithNeighbours[y - chunkColumnChunkYMin].chunkIndex = chunkIndex;
		
		//updateNeighbourChunks and move to next
		for(int i{}; i < neighbourDirsCount; i ++) {
			vec3i const offset{ neighbourDirs[i] };
			auto const optChunkIndex{ neighbourChunks[i].optChunk() };
			chunkIndicesWithNeighbours[y - chunkColumnChunkYMin].neighbours[i] = optChunkIndex;
			
			vec3 const next{ vec3i{0,-1,0} };
			neighbourChunks[i].offset(next);
		}
		topNeighbourIndex = chunk::OptionalChunkIndex{ chunkIndex };
	}
	
	for(auto const &[chunkIndex, neighbours] : chunkIndicesWithNeighbours) {		
		auto chunk{ chunks[chunkIndex] };
		
		auto const &aabb{ chunk.aabb() };
		
		auto const updateBlocksNoNeighbours{ !aabb.isEmpty() };
		auto const areaUpdateNoNeighbours{ intersectAreas3i(
			Area{ vec3i{-1} , vec3i{units::blocksInChunkDim-1 + 1} },
			{ aabb.first - 1, aabb.last + 1 }
		) };
		
		//AO is updated later in updateChunk()
		
		if(updateBlocksNoNeighbours) { //blocks with no neighbours
			updateBlocksDataInArea(chunk, pBlock{aabb.first}, pBlock{aabb.last});
			//chunk.status().setBlocksUpdated(true); //already set
		}
		
		for(int i{}; i < neighbourDirsCount; i++) {
			auto const &neighbourIndex{ neighbours[i] };
			if(!neighbourIndex.is()) continue;
			
			auto const offset{ neighbourDirs[i] };	
			auto neighbourChunk{ chunks[neighbourIndex.get()] };
			auto const &neighbourAabb{ neighbourChunk.aabb() };
			auto const neighbourFirst{ neighbourAabb.first };
			auto const neighbourLast { neighbourAabb.last  };

			if(!neighbourChunk.status().isUpdateAO()) { //AO
				auto const updatedAreaCubes{ intersectAreas3i(
					{ vec3i{0} - offset*units::cubesInChunkDim, vec3i{units::cubesInChunkDim} - offset*units::cubesInChunkDim },
					{ neighbourFirst * units::cubesInBlockDim, (neighbourLast+1) * units::cubesInBlockDim - 1 })
				};
				
				if(!updatedAreaCubes.isEmpty()) {
					updateAOInArea(neighbourChunk, pCube{updatedAreaCubes.first}, pCube{updatedAreaCubes.last});
					neighbourChunk.status().setAOUpdated(true);
				}
			}
			
			if(updateBlocksNoNeighbours) { //blocks with no neighbours
				Area const updatedAreaBlocks_{ 
					areaUpdateNoNeighbours.first - offset*units::blocksInChunkDim,
					areaUpdateNoNeighbours.last  - offset*units::blocksInChunkDim
				};
				auto const updatedAreaBlocks{ intersectAreas3i(
					updatedAreaBlocks_,
					{ 0, units::blocksInChunkDim-1 }
				) };
				
				if(!updatedAreaBlocks.isEmpty()) {
					updateBlocksDataInArea(neighbourChunk, pBlock{updatedAreaBlocks.first}, pBlock{updatedAreaBlocks.last});
					neighbourChunk.status().setBlocksUpdated(true);
				}
			}
			
			neighbourChunk.status().setUpdateNeighbouringEmitters(true);
		}
	}
	
	calculateLighting(chunks, chunkIndices, columnPosition, lowestEmptyY, lowestNotFullY);
}
