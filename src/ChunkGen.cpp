#include"ChunkGen.h"
#include"BlockProperties.h"
#include"Lighting.h"
#include"BlocksData.h"
#include"AO.h"
#include"Counter.h"
#include"Units.h"
#include"Counter.h"

#include"PerlinNoise.h"

#include<string>
#include<sstream>
#include<fstream>
#include<array>
#include<algorithm>
#include<type_traits>
#include<string_view>
#include<filesystem>
#include<chrono>


static std::string chunkFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save0/" << pos << ".cnk";
	return ss.str();
}

static std::string chunkNewFilename(chunk::Chunk const chunk) {
	auto const &pos{ chunk.position() };
	std::stringstream ss{};
	ss << "./save2/" << pos << ".cnk2";
	return ss.str();
}


static std::string chunkColumnFilename(vec2<pChunk::value_type::value_type> const xz) {
	static constexpr auto lookupSizeAsPow2{ 6 };
	static constexpr auto lookupSize{ 1 << lookupSizeAsPow2 };
	static constexpr long long oneCoordSize{ 5 };
	static_assert((((long long) units::chunkMax) - ((long long) units::chunkMin)) < lookupSize*lookupSize*lookupSize*lookupSize*lookupSize);
	static constexpr char const (&lookup)[lookupSize+1] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ=-";
	
	using ut = typename std::make_unsigned<decltype(xz)::value_type>::type;
		
	std::array<char, oneCoordSize*2 + 1> result;
	long long offset{};
	
	for(long long i{}; i < oneCoordSize; i++)
		result[offset++] = lookup[(ut(xz.x) >> (i * lookupSizeAsPow2)) & (lookupSize-1)];
	
	result[offset++] = '_';
	
	for(long long i{}; i < oneCoordSize; i++) 
		result[offset++] = lookup[(ut(xz.y) >> (i * lookupSizeAsPow2)) & (lookupSize-1)];
	
	std::stringstream ss{};
	ss << std::string_view{ &*std::begin(result), result.size() } << ".ccm";
	return ss.str();
}

static void writeChunksColumn(chunk::Chunks &chunks, vec2<pChunk::value_type::value_type> const xz, std::string_view const worldName) {	
	std::stringstream ss{};
	ss << "./save/" << worldName << '/';
	std::filesystem::create_directories(ss.str());
	ss << chunkColumnFilename(xz);
	
	std::ofstream file{ ss.str(), std::ios::binary };
	if(!file) { std::cout << "ERROR: could not write chunk column " << xz << '\n'; return; }
	
	uint32_t const version{};
	file.write((char*) &version, sizeof(version));
	
	chunk::MovingChunk curMChunk{ chunks, vec3i{xz.x, chunkColumnChunkYMin, xz.y} };
	for(auto y{chunkColumnChunkYMin}; y <= chunkColumnChunkYMax; y++) {
		auto chunk{ curMChunk.get() };
		
		chunk.modified() = false;
		
		//blocks aabb
		auto const aabb{ chunk.aabb() };
		chunk::PackedAABB<pBlock> const aabbData{ aabb };
		file.write((char*) &aabbData, sizeof(aabbData));
			
		//blocks
		auto const &blocks{ chunk.blocks() };
		iterateArea(aabb, [&](pBlock const blockCoord) {
			auto const &block{ blocks[blockCoord] };
			auto const id{ block.id() };
			auto const cubes{ block.cubes() };
			
			file.write((char*) &id, sizeof(id));
			file.write((char*) &cubes, sizeof(cubes));
		});
		
		//liquid aabb
		auto const blocksData{ chunk.blocksData() };
		auto liquidAABB{ Area::empty() };
		iterateArea(aabb, [&](pBlock const blockCoord) {
			auto const &blockData{ blocksData[blockCoord] };
			
			for(int i{}; i < pos::cubesInBlockCount; i++) {
				auto const cubeInBlockCoord{ chunk::cubeIndexInBlockToCoord(i) };
				auto const cubeCoord{ blockCoord + cubeInBlockCoord };
				
				if(blockData.liquidCubes & (1 << i)) liquidAABB += cubeCoord.val();
			}
		});
		chunk::PackedAABB<pCube> liquidAABBData{ liquidAABB };
		file.write((char*) &liquidAABBData, sizeof(liquidAABBData));
		
		//liquid
		auto const liquid{ chunk.liquid() };
		iterateArea(liquidAABB, [&](pCube const cubeCoord) {
			auto const &liquidCube{ liquid[cubeCoord] };
			auto const id{ liquidCube.id };
			auto const level{ liquidCube.level };
			
			file.write((char*) &id, sizeof(id));
			file.write((char*) &level, sizeof(level));
		});
		
		curMChunk = curMChunk.offseted(vec3i{0, 1, 0});
	}
	
	file.close();
}

struct ReadStatus {
	bool all;
	std::array<bool, chunksCoumnChunksCount> is;
};

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

static ReadStatus readChunksColumn(
	chunk::Chunks &chunks, int (&chunksIndices)[chunksCoumnChunksCount], vec2<pChunk::value_type::value_type> const xz, 
	std::string_view const worldName
) {	
	std::stringstream ss{};
	ss << "./save/" << worldName << '/';
	std::filesystem::create_directories(ss.str());
	ss << chunkColumnFilename(xz);
	
	std::ifstream file{ ss.str(), std::ios::binary };
	if(!file) { 
		ReadStatus rs{};
		rs.all = true;
		
		for(auto i{0}; i < chunksCoumnChunksCount; i++) {
			auto chunk{ chunks[chunksIndices[i]] };
			auto const is{ (rs.is[i] =/*!*/ tryReadChunk(chunk)) };
			rs.all = rs.all && is;
			if(is) chunk.modified() = true; //if chunk is read fromb old file, write it later using new method
		}
		
		return rs;
	}
	
	uint32_t version{};
	file.read((char*) &version, sizeof(version));
	assert(version == 0);

	for(auto y{chunkColumnChunkYMin}; y <= chunkColumnChunkYMax; y++) {
		auto const chunkIndex{ chunksIndices[y - chunkColumnChunkYMin] };
		auto chunk{ chunks[chunkIndex] };
		auto &emitters{ chunk.emitters() };
		
		//blocks aabb
		chunk::PackedAABB<pBlock> aabbData;
		file.read((char*) &aabbData, sizeof(aabbData));
		auto const aabb{ chunk.aabb() = Area{ aabbData.first().val(), aabbData.last().val() } };
		
		//blocks
		auto &blocks{ chunk.blocks() };
		iterateArea(aabb, [&](pBlock const blockCoord) {
			auto &block{ blocks[blockCoord] };
			
			chunk::Block::id_t id;
			chunk::Block::cubes_t cubes;
			file.read((char*) &id, sizeof(id));
			file.read((char*) &cubes, sizeof(cubes));
			
			block = { id, cubes };
			if(isBlockEmitter(id)) emitters.add(blockCoord.val());
		});
		
		//liquid aabb
		chunk::PackedAABB<pCube> liquidAABBData;
		file.read((char*) &liquidAABBData, sizeof(liquidAABBData));
		Area liquidAABB{ liquidAABBData.first().val(), liquidAABBData.last().val() };
		
		//liquid
		auto &liquid{ chunk.liquid() };
		iterateArea(liquidAABB, [&](pCube const cubeCoord) {
			auto &liquidCube{ liquid[cubeCoord] };
			chunk::Block::id_t id;
			chunk::LiquidCube::level_t level;
			
			file.read((char*) &id, sizeof(id));
			file.read((char*) &level, sizeof(level));
			
			liquidCube = chunk::LiquidCube::liquid( 
				id, 
				level, 
				false/*
					note: falling flag is not saved, 
					so false is used because even if liquid was falling down, 
					incorrect flag will only affect how this cube is rendered
				*/
			);
			chunks.liquidCubes.add({ chunkIndex, chunk::cubeCoordToIndex(cubeCoord) });
		});
	}

	file.close();
	
	return { true, {true} };
}

void writeChunk(chunk::Chunk chunk, std::string_view const worldName) {
	writeChunksColumn(chunk.chunks(), chunk.position().xz(), worldName);
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
				
				if(curBlock.id() == 0 || curBlock.id() == 16) {
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
	auto &chunks{ chunk.chunks() };
	auto const chunkIndex{ chunk.chunkIndex() };
	auto const &pos{ chunk.position() };
	auto &blocks{ chunk.data() };
	auto &liquid{ chunk.liquid() };
	auto &emitters{ chunk.emitters() };
	chunk.modified() = true;
	
	auto &aabbLoc{ chunk.aabb() = Area::empty() };
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
			if(diff >= -1) {
				blocks[blockCoord] = chunk::Block::fullBlock(16);
				aabb += {blockCoord};
			}
			else blocks[blockCoord] = chunk::Block::emptyBlock();
			
			if(pos.y * units::blocksInChunkDim + y == 7) {
				aabb += {blockCoord};
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					pCube const cubeLocalCoord{ chunk::Block::cubeIndexPos(cubeIndex) };
					auto const cubeCoord{ pBlock{blockCoord} + cubeLocalCoord }; 
					
					chunks.liquidCubes.add({ chunkIndex, chunk::cubeCoordToIndex(cubeCoord) });
					
					static_assert(chunk::LiquidCube::maxLevel >/*strictly greater!*/ 254u);
					if(cubeLocalCoord.val().y == units::cubesInBlockDim-1)
						liquid[cubeCoord] = chunk::LiquidCube::liquid(15, 254u, false);
					else 
						liquid[cubeCoord] = chunk::LiquidCube::liquid(15, chunk::LiquidCube::maxLevel, false);
				}
			}
			if(pos.y * units::blocksInChunkDim + y < 7) {
				aabb += {blockCoord};
				
				for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
					auto const cubeCoord{ pBlock{blockCoord} + pCube{ chunk::Block::cubeIndexPos(cubeIndex) } }; 
					liquid[cubeCoord] = chunk::LiquidCube::liquid(15, chunk::LiquidCube::maxLevel, false);
					chunks.liquidCubes.add({ chunkIndex, chunk::cubeCoordToIndex(cubeCoord) });
				}
			}
		}
		
		if(isBlockEmitter(blocks[blockCoord].id())) emitters.add(blockCoord);
	}
	
	aabbLoc = aabb;
	genTrees(chunk);	
}

static void fillChunksData(
	chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], vec2<pChunk::value_type::value_type> const xz, 
	std::string_view const worldName,bool const loadChunks
) {
	ReadStatus rs{};
	if(loadChunks) rs = readChunksColumn(chunks, chunkIndices, xz, worldName);
	
	if(!rs.all) {
		//generate heights for each block column in the chunk column
		double heights[units::blocksInChunkDim * units::blocksInChunkDim];
		auto minHeight{ std::numeric_limits<double>::infinity() };
		for(int z{}; z < units::blocksInChunkDim; z++) 
		for(int x{}; x < units::blocksInChunkDim; x++) {
			auto const height{  heightAt(vec2i{xz.x,xz.y}, vec2i{x,z}) };
			heights[z* units::blocksInChunkDim + x] = height;
			minHeight = std::min(minHeight, height);
		}
		
		for(int i{}; i < chunksCoumnChunksCount; i++) {
			if(rs.is[i]) continue;
			genChunkData(heights, chunks[chunkIndices[i]]);
		}
	}
}

extern long long diff__;

void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition, std::string_view const worldName, bool const loadChunks) {
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
	auto emptyBefore{ true };
	
	struct ChunkIndexAndNeighbours{ 
		int chunkIndex; 
		chunk::OptionalChunkIndex neighbours[neighbourDirsCount];
	};
	ChunkIndexAndNeighbours chunkIndicesWithNeighbours[chunksCoumnChunksCount];
	
	//auto const start{ std::chrono::steady_clock::now() };
	
	//setup chunks
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
		chunk.blocks().reset();
		//blocks data is filled later
		chunk.ao().reset();
		
		chunk.blockLighting().reset();
		//sky lighting is filled later
		chunk.emitters().clear();
		chunk.neighbouringEmitters().clear();
		
	}
	
	//auto const setup{ std::chrono::steady_clock::now() };
	
	fillChunksData(chunks, chunkIndices, columnPosition, worldName, loadChunks);
	
	//auto const fill{ std::chrono::steady_clock::now() };
	
	//setup neighbours and update data inside chunks
	for(int i {chunksCoumnChunksCount-1}; i >= 0; i--) {
		auto const y{ i + chunkColumnChunkYMin };
		auto const chunkIndex{ chunkIndices[i] };
		auto chunk{ chunks[chunkIndex] };
		
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
		
		updateBlocksDataWithoutNeighboursInArea(chunk, pBlock{0}, pBlock{units::blocksInChunkDim-1});
		
		if(emptyBefore && chunk.aabb().isEmpty()) {
			chunk.skyLighting().fill(chunk::ChunkLighting::maxValue);
			lowestEmptyY = misc::min(lowestEmptyY, y);
		}
		else {
			chunk.skyLighting().reset();
			emptyBefore = false;
		}
		
		//updateNeighbourChunks and move to next
		chunkIndicesWithNeighbours[y - chunkColumnChunkYMin].chunkIndex = chunkIndex;
		
		for(int j{}; j < neighbourDirsCount; j ++) {
			vec3i const offset{ neighbourDirs[j] };
			auto const optChunkIndex{ neighbourChunks[j].optChunk() };
			chunkIndicesWithNeighbours[i].neighbours[j] = optChunkIndex;
			
			vec3 const next{ vec3i{0,-1,0} };
			neighbourChunks[j].offset(next);
		}
		topNeighbourIndex = chunk::OptionalChunkIndex{ chunkIndex };
	}
	
	//auto const data{ std::chrono::steady_clock::now() };
	
	//update AO and blocks neighbours info 
	for(auto const &[chunkIndex, neighbours] : chunkIndicesWithNeighbours) {		
		auto chunk{ chunks[chunkIndex] };
		
		auto const &aabb{ chunk.aabb() };
		
		auto const areaUpdateNoNeighbours{ intersectAreas3i(
			Area{ vec3i{-1} , vec3i{units::blocksInChunkDim-1 + 1} },
			{ aabb.first - 1, aabb.last + 1 }
		) };
		
		{ //updates for current chunk
			assert(chunk.status().isUpdateAO()); //AO is updated later in updateChunk()
		
			updateBlocksDataNeighboursInfoInArea(chunk, pBlock{aabb.first}, pBlock{aabb.last});
			assert(chunk.status().isBlocksUpdated()); //chunk.status().setBlocksUpdated(true); //already set
		}
		
		//updates for neighbouring chunks
		for(int i{}; i < neighbourDirsCount; i++) {
			auto const &neighbourIndex{ neighbours[i] };
			if(!neighbourIndex.is()) continue;
			
			auto const offset{ neighbourDirs[i] };	
			auto neighbourChunk{ chunks[neighbourIndex.get()] };
			auto const &neighbourAabb{ neighbourChunk.aabb() };
			
			if(!neighbourChunk.status().isUpdateAO()) { //AO
				auto const updatedAreaCubes{ intersectAreas3i(
					{ vec3i{0} - offset*units::cubesInChunkDim, vec3i{units::cubesInChunkDim} - offset*units::cubesInChunkDim },
					{ neighbourAabb.first * units::cubesInBlockDim, (neighbourAabb.last+1) * units::cubesInBlockDim - 1 })
				};
				
				updateAOInArea(neighbourChunk, pCube{updatedAreaCubes.first}, pCube{updatedAreaCubes.last});
				neighbourChunk.status().setAOUpdated(true);
			}
			
			{ //neighbours info
				Area const updatedAreaBlocks_{ 
					areaUpdateNoNeighbours.first - offset*units::blocksInChunkDim,
					areaUpdateNoNeighbours.last  - offset*units::blocksInChunkDim
				};
				auto const updatedAreaBlocks{ intersectAreas3i(
					updatedAreaBlocks_,
					{ 0, units::blocksInChunkDim-1 }
				) };
				
				updateBlocksDataNeighboursInfoInArea(neighbourChunk, pBlock{updatedAreaBlocks.first}, pBlock{updatedAreaBlocks.last});
				neighbourChunk.status().setBlocksUpdated(true);
			}
			
			neighbourChunk.status().setUpdateNeighbouringEmitters(true);
		}
	}
	
	
	//auto const ao{ std::chrono::steady_clock::now() };
	
	calculateLighting(chunks, chunkIndices, columnPosition, lowestEmptyY);
	
	//auto const lighting{ std::chrono::steady_clock::now() };
	
	//Counter<256> setup_{}, fill_{}, data_{}, ao_{}, lighting_{};
	//
	//#define a(PREV_COUNTER, COUNTER) COUNTER##_.add( std::chrono::duration_cast<std::chrono::microseconds>(COUNTER - PREV_COUNTER).count() );
	//
	//a(start, setup)
	//a(setup, fill)
	//a(fill, data)
	//a(data, ao)
	//a(ao, lighting)
	//
	//#undef a
	//#define b(COUNTER) (COUNTER##_.mean()/1000.0) << ' ' <<
	//
	//std::cout << b(setup) b(fill) b(data) b(ao) b(lighting) '\n';
	//#undef b
}
