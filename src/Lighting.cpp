#include"Lighting.h"
#include"Units.h"
#include"Area.h"
#include"LightingPropagation.h"

struct Sides {
	bool sides[4]; //(-1, 0, 0), (0, 0, -1), (1, 0, 0), (0, 0, 1)
	
	bool &operator[](int const i) {
		assert(i >= 0 && i < 4);
		return sides[i];
	}
	
	bool const &operator[](int const i) const {
		assert(i >= 0 && i < 4);
		return sides[i];
	}
	
	bool any() const {
		return sides[0] || sides[1] || sides[2] || sides[3];
	}
};

template<typename Action>
inline void iterateCubeNeighboursInColumn(
	int const y, vec3i const cubeCoord, 
	Action &&action
) {
	for(auto i{decltype(chunk::ChunkLighting::dirsCount){}}; i < chunk::ChunkLighting::dirsCount; i++) {
		auto const neighbourDir{ chunk::ChunkLighting::indexAsDir(i) };
		
		pCube const neighbourCoord{ cubeCoord + neighbourDir };
		auto const neighbourOffsetChunk{ neighbourCoord.valAs<pChunk>() };
		if(neighbourOffsetChunk * vec3i{1, 0, 1} != 0) continue;
		
		auto const neighbourInChunkCoord{ neighbourCoord - pChunk{neighbourOffsetChunk} };
		
		auto const neighbourChunkY{ y + neighbourOffsetChunk.y };
		if(neighbourChunkY >= chunkColumnChunkYMin && neighbourChunkY <= chunkColumnChunkYMax); else continue;

		action(neighbourDir, neighbourChunkY, neighbourInChunkCoord.val());
	}
}

template<typename Config>
static void propagateAddLightInColumn(
	chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount],
	int const startChunkY, vec3i const startCubeInChunkCoord, uint8_t const startLight
) {
	iterateCubeNeighboursInColumn(
		startChunkY, startCubeInChunkCoord, 
		[&](vec3i const fromDir, int const y, vec3i const cubeInChunkCoord) -> void {
			auto cubeChunk{ chunks[chunkIndices[y - chunkColumnChunkYMin]] };
			
			auto const cube{ cubeChunk.data().cubeAt(cubeInChunkCoord) };
			auto const blockId{ cube.block.id() };
			auto const isCube{ cube.isSolid };
			
			auto const type{ Config::getType(blockId, isCube) };
			if(type != LightingCubeType::medium) return;
			
			auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
			auto const expectedLight{ Config::propagationRule(startLight, fromDir, blockId, isCube) };
			if(expectedLight > cubeLight) {
				cubeLight = expectedLight;
				propagateAddLightInColumn<Config>(chunks, chunkIndices, y, cubeInChunkCoord, cubeLight);	
			}
			
		}
	);
}

template<typename Config>
static void fromCubeInColumn(chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], int const y, vec3i const cubeInChunkCoord) {	
	auto const startLight{ Config::getLight(chunks[chunkIndices[y - chunkColumnChunkYMin]], cubeInChunkCoord) };
	if(startLight == 0) return;
	propagateAddLightInColumn<Config>(chunks, chunkIndices, y, cubeInChunkCoord, startLight);
}

static void propagateHorisontalLightingIn(
	chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], 
	int const highestChunkY, int const lowestChunkY, 
	bool const skyLighting,
	Sides (*updatedChunks)[chunksCoumnChunksCount] = nullptr
) {
	if(!updatedChunks) {
		static Sides ignored[chunksCoumnChunksCount];
		updatedChunks = &ignored;
	}
		
	auto const lightingConfig{ skyLighting ? 
		  ConfigInstance::from<SkyLightingConfig>()
		: ConfigInstance::from<BlocksLightingConfig>() 
	};
	
	static constexpr vec3i updateSides[] = { {1, 0, 0}, {0, 0, 1} };
	static constexpr vec3i otherAxiss [] = { {0, 0, 1}, {1, 0, 0} };
	
	for(int chunkY{ highestChunkY }; chunkY >= lowestChunkY; chunkY--) {
		auto const chunkI{ chunkY - chunkColumnChunkYMin };
		auto chunk{ chunks[chunkIndices[chunkI]] };
		
		for(int positive_{}; positive_ < 2; positive_++) {
			bool positive( positive_ );
			auto const sign{ positive ? 1 : -1 };
			
			for(int axisIndex{}; axisIndex < 2; axisIndex++) {
				auto const axis{ updateSides[axisIndex] };
				auto const oAxis1{ vec3i{ 0, 1, 0 } };
				auto const oAxis2{ otherAxiss[axisIndex] };
				
				auto const toNeighbourDir{ axis * sign };
				auto optNeighbourChunk{ chunk::MovingChunk{ chunk }.offseted(toNeighbourDir) };
				if(!optNeighbourChunk.is()) continue;
				auto neighbourChunk{ optNeighbourChunk.get() };
				
				auto const &chunkBlocks{ chunk.data() };
				auto &chunkLighting{ lightingConfig.getLighting(chunk) };	
				
				auto &neighbourChunkLighting{ lightingConfig.getLighting(neighbourChunk) };	
				
				auto const &aabb{ chunk.aabb() };
				pBlock const firstInChunkPos{
					axis * (positive ? units::blocksInChunkDim-1 : 0)
					+ oAxis1 * 0
					+ oAxis2 * 0
				};
				pBlock const lastInChunkPos{
					axis   * (positive ? units::blocksInChunkDim-1 : 0)
					+ oAxis1 * (units::blocksInChunkDim-1)
					+ oAxis2 * (units::blocksInChunkDim-1)
				};
				auto const testBlocksArea{ intersectAreas3i(
					{ aabb.start()         , aabb.end()           },
					{ firstInChunkPos.val(), lastInChunkPos.val() }
				) };
				
				for(int o1{0}; o1 < units::cubesInChunkDim; o1++)
				for(int o2{0}; o2 < units::cubesInChunkDim; o2++) {
					pCube const cubePosInChunk{
						axis   * (positive ? units::cubesInChunkDim-1 : 0)
						+ oAxis1 * o1
						+ oAxis2 * o2
					};
					
					pCube const neighbourCubePosInNeighbourChunk{
						axis   * (positive ? 0 : units::cubesInChunkDim-1)
						+ oAxis1 * o1
						+ oAxis2 * o2
					};
					
					auto const chunkCube{ [&]() {
						if(testBlocksArea.contains(cubePosInChunk.valAs<pBlock>())) {
							return chunkBlocks.cubeAt(cubePosInChunk.val());   
						}
						else return chunk::ChunkData::Cube{};
					}() };
					
					if(lightingConfig.getType(chunkCube.block.id(), chunkCube.isSolid) != LightingCubeType::medium) continue;
					
					auto &cubeLight               { chunkLighting         [cubePosInChunk                  .val()] };
					auto const &neighbourCubeLight{ neighbourChunkLighting[neighbourCubePosInNeighbourChunk.val()] };
					
					auto const toChunkCubeLighting{ 
						lightingConfig.propagationRule(neighbourCubeLight, -toNeighbourDir, chunkCube.block.id(), chunkCube.isSolid)
					};
					if(toChunkCubeLighting > cubeLight) {
						cubeLight = toChunkCubeLighting;
						(*updatedChunks)[chunkI][axisIndex + positive_ * 2] = true;
					}	
				}
			}
		}
	}
}

void calculateLighting(chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], 
vec2i const columnPosition, int const lowestEmptyY, int const lowestNotFullY) {
	{//sky lighting
		{ //sky cuhnks
			/* Iterate over emptyChunks with maximum sky lighting's neighbours.
				If the neighbour can accept some lighting frim that chunk, call setUpdateLightingAdd(true);
			*/
			static constexpr vec3i updateSides[] = { {1, 0, 0}, {0, 0, 1} };
			static constexpr vec3i otherAxiss [] = { {0, 0, 1}, {1, 0, 0} };
			
			for(int y{ chunkColumnChunkYMax }; y >= lowestEmptyY; y--) {
				auto chunk{ chunks[chunkIndices[y - chunkColumnChunkYMin]] };
				
				for(int positive_{}; positive_ < 2; positive_++) {
					bool positive( positive_ );
					auto const sign{ positive ? 1 : -1 };
					
					for(int axisIndex{}; axisIndex < 2; axisIndex++) {
						auto const axis{ updateSides[axisIndex] };
						auto const oAxis1{ vec3i{ 0, 1, 0 } };
						auto const oAxis2{ otherAxiss[axisIndex] };
						
						auto const toNeighbourDir{ axis * sign };
						auto optNeighbourChunk{ chunk::MovingChunk{ chunk }.offseted(toNeighbourDir) };
						if(!optNeighbourChunk.is()) continue;
						auto neighbourChunk{ optNeighbourChunk.get() };
						
						auto const &neighbourChunkBlocks{ neighbourChunk.data() };
						auto &neighbourChunkLighting{ SkyLightingConfig::getLighting(neighbourChunk) };
						
						auto const &neighbourAabb{ neighbourChunk.aabb() };
						pBlock const firstInNeighbourChunkPos{
							axis * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * 0
							+ oAxis2 * 0
						};
						pBlock const lastInNeighbourChunkPos{
							axis   * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * (units::blocksInChunkDim-1)
							+ oAxis2 * (units::blocksInChunkDim-1)
						};
						auto const testNeighbourBlocksArea{ intersectAreas3i(
							{ neighbourAabb.start()         , neighbourAabb.end()           },
							{ firstInNeighbourChunkPos.val(), lastInNeighbourChunkPos.val() }
						) };
	
						for(int o1{0}; o1 < units::cubesInChunkDim; o1++)
						for(int o2{0}; o2 < units::cubesInChunkDim; o2++) {
							pCube const cubePosInChunk{
								axis   * (positive ? units::cubesInChunkDim-1 : 0)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							pCube const neighbourCubePosInNeighbourChunk{
								axis   * (positive ? 0 : units::cubesInChunkDim-1)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							auto const toCube{ [&]() {
								if(testNeighbourBlocksArea.contains(neighbourCubePosInNeighbourChunk.valAs<pBlock>())) {
									return neighbourChunkBlocks.cubeAt(neighbourCubePosInNeighbourChunk.val());   
								}
								else return chunk::ChunkData::Cube{};
							}() };
							
							if(SkyLightingConfig::getType(toCube.block.id(), toCube.isSolid) != LightingCubeType::medium) continue;
							
							auto const cubeLight         { chunk::ChunkLighting::maxValue };
							auto const neighbourCubeLight{ neighbourChunkLighting[neighbourCubePosInNeighbourChunk.val()] };
							
							auto const toNeighbourCubeLighting{ 
								SkyLightingConfig::propagationRule(cubeLight, toNeighbourDir, toCube.block.id(), toCube.isSolid)
							};
							if(toNeighbourCubeLighting > neighbourCubeLight) {
								neighbourChunk.status().setUpdateLightingAdd(true);
								goto nextChunk;
							}
						}
					}
					
					nextChunk:;
				}
			}
		}
		
		{ //downward lighting in chunks
			for(int chunkY{ lowestEmptyY-1 }; chunkY >= lowestNotFullY; chunkY--) {
				auto chunk{ chunks[chunkIndices[chunkY - chunkColumnChunkYMin]] };
				
				auto const &aabb{ chunk.aabb() };
				Area<vec3i> const blocksArea{ aabb.start(), aabb.end() };
				
				auto &chunkLighting{ SkyLightingConfig::getLighting(chunk) };
				auto const &chunkLightingBefore{ [&](){
					if(chunkY == chunkColumnChunkYMax) return skyLighting;
					else return SkyLightingConfig::getLighting(chunks[chunkIndices[chunkY - chunkColumnChunkYMin + 1]]);
				}() }; //igher up
				
				auto const &chunkBlocks{ chunk.data() };
				
				vec2i layerLightingNot0First{ units::cubesInChunkDim };
				vec2i layerLightingNot0Last { -1 };
				
				//first layer
				for(int z{}; z < units::cubesInChunkDim; z++)
				for(int x{}; x < units::cubesInChunkDim; x++) {
					pCube const cubePos{ x, units::cubesInChunkDim-1, z };
					pCube const cubePosBefore{ x, 0, z };
					
					auto const cube{ [&]() {
						if(blocksArea.contains(cubePos.valAs<pBlock>())) return chunkBlocks.cubeAt(cubePos.val());
						else return chunk::ChunkData::Cube();
					}() };
					
					if(SkyLightingConfig::getType(cube.block.id(), cube.isSolid) != LightingCubeType::medium) continue;
					
					auto const cubeBeforeLighting{ chunkLightingBefore[cubePosBefore.val()] };
					
					auto const toCubeLighting{ 
						SkyLightingConfig::propagationRule(cubeBeforeLighting, vec3i{0, -1, 0}, cube.block.id(), cube.isSolid) 
					};
					
					if(toCubeLighting > chunk::ChunkLighting::minValue) {
						chunkLighting[cubePos.val()] = toCubeLighting;
						layerLightingNot0First = layerLightingNot0First.min(vec2i{x, z});
						layerLightingNot0Last  = layerLightingNot0Last .max(vec2i{x, z});
					}
					
				}
				
				if((layerLightingNot0First > layerLightingNot0Last).any()) goto stopPropagation;
				
				for(auto y{units::cubesInChunkDim-1 - 1}; y >= 0; y--) {
					vec2i layerLightingNot0FirstNew{ units::cubesInChunkDim };
					vec2i layerLightingNot0LastNew { -1 };
					
					for(int z{layerLightingNot0First.y}; z <= layerLightingNot0Last.y; z++)
					for(int x{layerLightingNot0First.x}; x <= layerLightingNot0Last.x; x++) {
						pCube const cubePos{ x, y, z };
						pCube const cubePosBefore{ x, y + 1, z };
						
						auto const cube{ [&]() {
							if(blocksArea.contains(cubePos.valAs<pBlock>())) return chunkBlocks.cubeAt(cubePos.val());
							else return chunk::ChunkData::Cube();
						}() };
						
						if(SkyLightingConfig::getType(cube.block.id(), cube.isSolid) != LightingCubeType::medium) continue;
						
						auto const cubeBeforeLighting{ chunkLighting[cubePosBefore.val()] };
						
						auto const toCubeLighting{ 
							SkyLightingConfig::propagationRule(cubeBeforeLighting, vec3i{0, -1, 0}, cube.block.id(), cube.isSolid) 
						};
						
						if(toCubeLighting > chunk::ChunkLighting::minValue) {
							chunkLighting[cubePos.val()] = toCubeLighting;
							layerLightingNot0FirstNew = layerLightingNot0FirstNew.min(vec2i{x, z});
							layerLightingNot0LastNew  = layerLightingNot0LastNew .max(vec2i{x, z});
						}
					}
					
					layerLightingNot0First = layerLightingNot0FirstNew;
					layerLightingNot0Last  = layerLightingNot0LastNew ;
					
					if((layerLightingNot0First > layerLightingNot0Last).any()) goto stopPropagation;
				}
			}
			
			stopPropagation:;
		}
		
		{ //propagate borders lighting in
			propagateHorisontalLightingIn(chunks, chunkIndices, lowestEmptyY-1, lowestNotFullY, true); 
		}
		
		{ //propagate inside chunks
			for(int chunkY{ lowestEmptyY-1 }; chunkY >= lowestNotFullY; chunkY--) {
				for(int i{}; i < pos::cubesInChunkCount; i++) {
					fromCubeInColumn<SkyLightingConfig>(chunks, chunkIndices, chunkY, chunk::cubeCoordInChunk(i));
				}
			}
		}
		
		{ //propagate borders lighting out
			static constexpr vec3i updateSides[] = { {1, 0, 0}, {0, 0, 1} };
			static constexpr vec3i otherAxiss [] = { {0, 0, 1}, {1, 0, 0} };
			
			for(int chunkY{ lowestEmptyY-1 }; chunkY >= lowestNotFullY; chunkY--) {
				auto chunk{ chunks[chunkIndices[chunkY - chunkColumnChunkYMin]] };
				
				for(int positive_{}; positive_ < 2; positive_++) {
					bool positive( positive_ );
					auto const sign{ positive ? 1 : -1 };
					
					for(int axisIndex{}; axisIndex < 2; axisIndex++) {
						auto const axis{ updateSides[axisIndex] };
						auto const oAxis1{ vec3i{ 0, 1, 0 } };
						auto const oAxis2{ otherAxiss[axisIndex] };
						
						auto const toNeighbourDir{ axis * sign };
						auto optNeighbourChunk{ chunk::MovingChunk{ chunk }.offseted(toNeighbourDir) };
						if(!optNeighbourChunk.is()) continue;
						auto neighbourChunk{ optNeighbourChunk.get() };
						
						auto &chunkLighting{ SkyLightingConfig::getLighting(chunk) };	
						
						auto const &neighbourChunkBlocks{ neighbourChunk.data() };
						auto &neighbourChunkLighting{ SkyLightingConfig::getLighting(neighbourChunk) };
						
						auto const &neighbourAabb{ neighbourChunk.aabb() };
						pBlock const firstInNeighbourChunkPos{
							axis * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * 0
							+ oAxis2 * 0
						};
						pBlock const lastInNeighbourChunkPos{
							axis   * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * (units::blocksInChunkDim-1)
							+ oAxis2 * (units::blocksInChunkDim-1)
						};
						auto const testNeighbourBlocksArea{ intersectAreas3i(
							{ neighbourAabb.start()         , neighbourAabb.end()           },
							{ firstInNeighbourChunkPos.val(), lastInNeighbourChunkPos.val() }
						) };
						
						for(int o1{0}; o1 < units::cubesInChunkDim; o1++)
						for(int o2{0}; o2 < units::cubesInChunkDim; o2++) {
							pCube const cubePosInChunk{
								axis   * (positive ? units::cubesInChunkDim-1 : 0)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							pCube const neighbourCubePosInNeighbourChunk{
								axis   * (positive ? 0 : units::cubesInChunkDim-1)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							auto const neighbourCube{ [&]() {
								if(testNeighbourBlocksArea.contains(neighbourCubePosInNeighbourChunk.valAs<pBlock>())) {
									return neighbourChunkBlocks.cubeAt(neighbourCubePosInNeighbourChunk.val());   
								}
								else return chunk::ChunkData::Cube{};
							}() };
							
							if(SkyLightingConfig::getType(neighbourCube.block.id(), neighbourCube.isSolid) != LightingCubeType::medium) continue;
							
							auto const &cubeLight   { chunkLighting         [cubePosInChunk                  .val()] };
							auto &neighbourCubeLight{ neighbourChunkLighting[neighbourCubePosInNeighbourChunk.val()] };
							
							auto const toNeighbourCubeLighting{ 
								SkyLightingConfig::propagationRule(cubeLight, toNeighbourDir, neighbourCube.block.id(), neighbourCube.isSolid)
							};
							if(toNeighbourCubeLighting > neighbourCubeLight) {
								neighbourChunk.status().setUpdateLightingAdd(true);
								goto nextChunk2;
							}
						}
						nextChunk2:;
					}
				}
			}
		}
	}

	{//block lighting
		Sides invalidated[chunksCoumnChunksCount] = {}; //I hope it is 0-initialized
		
		//propagate borders lighting in
		propagateHorisontalLightingIn(
			chunks, chunkIndices, 
			chunkColumnChunkYMax, lowestNotFullY, 
			false,
			&invalidated
		);
		
		constexpr bool blockLightingInColumn = false/*previous lighting calculations semm to be faster*/;
		
		{ //propagate inside chunks
			for(int chunkY{chunkColumnChunkYMax}; chunkY >= lowestNotFullY; chunkY--) {
				auto const chunkI{ chunkY - chunkColumnChunkYMin };
				
				auto const sidesInvalidated{ invalidated[chunkI] };
				if(!sidesInvalidated.any()) continue;
				
				auto chunk{ chunks[chunkIndices[chunkI]] };
				
				for(int i{}; i < 4; i++) {
					if(!sidesInvalidated[i]) continue;
					
					vec3i const axiss [] = { {1, 0, 0}, {0, 0, 1} };
					vec3i const oAxiss[] = { {0, 0, 1}, {1, 0, 0} };
					int   const signs [] = { -1, 1 };
					
					auto const axis    { axiss [i % 2]  };
					auto const oAxis1  { vec3i{0, 1, 0} };
					auto const oAxis2  { oAxiss[i % 2]  };
					auto const sign    { signs [i / 2]  };
					bool const positive(        i / 2   );
					
					for(int o1{}; o1 < units::cubesInChunkDim; o1++)
					for(int o2{}; o2 < units::cubesInChunkDim; o2++) {
						pCube const cubePos{
							  oAxis1 * o1
							+ oAxis2 * o2
							+ axis   * (positive ? units::cubesInChunkDim-1 : 0)
						};
						
						AddLighting::fromCube<BlocksLightingConfig>(chunk, cubePos.val());
					}
					
					auto const &blocks{ chunk.data() };
					
					for(auto const emitterBlockIndex : chunk.emitters()) {
						pBlock const emitterBlockPos{ chunk::indexBlock(emitterBlockIndex) };
						auto const block{ blocks[emitterBlockPos.val()] };
						for(int cubeIndex{}; cubeIndex < pos::cubesInBlockCount; cubeIndex++) {
							if(!block.cube(cubeIndex)) continue;
							
							auto const cubePos{ emitterBlockPos + pCube{chunk::Block::cubeIndexPos(cubeIndex)} };
							
							if constexpr (blockLightingInColumn) 
								 fromCubeInColumn<BlocksLightingConfig>(chunks, chunkIndices, chunkY, cubePos.valAs<pCube>());
							else AddLighting::fromCube<BlocksLightingConfig>(chunk, cubePos.valAs<pCube>());
						}
					}
				}
			}
		}
		
		if constexpr(blockLightingInColumn) { //propagate borders lighting out
			static constexpr vec3i updateSides[] = { {1, 0, 0}, {0, 0, 1} };
			static constexpr vec3i otherAxiss [] = { {0, 0, 1}, {1, 0, 0} };
			
			for(int chunkY{ chunkColumnChunkYMax }; chunkY >= lowestNotFullY; chunkY--) {
				auto chunk{ chunks[chunkIndices[chunkY - chunkColumnChunkYMin]] };
				
				for(int positive_{}; positive_ < 2; positive_++) {
					bool positive( positive_ );
					auto const sign{ positive ? 1 : -1 };
					
					for(int axisIndex{}; axisIndex < 2; axisIndex++) {
						auto const axis{ updateSides[axisIndex] };
						auto const oAxis1{ vec3i{ 0, 1, 0 } };
						auto const oAxis2{ otherAxiss[axisIndex] };
						
						auto const toNeighbourDir{ axis * sign };
						auto optNeighbourChunk{ chunk::MovingChunk{ chunk }.offseted(toNeighbourDir) };
						if(!optNeighbourChunk.is()) continue;
						auto neighbourChunk{ optNeighbourChunk.get() };
						
						auto &chunkLighting{ BlocksLightingConfig::getLighting(chunk) };	
						
						auto const &neighbourChunkBlocks{ neighbourChunk.data() };
						auto &neighbourChunkLighting{ BlocksLightingConfig::getLighting(neighbourChunk) };
						
						auto const &neighbourAabb{ neighbourChunk.aabb() };
						pBlock const firstInNeighbourChunkPos{
							axis * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * 0
							+ oAxis2 * 0
						};
						pBlock const lastInNeighbourChunkPos{
							axis   * (positive ? 0 : units::blocksInChunkDim-1)
							+ oAxis1 * (units::blocksInChunkDim-1)
							+ oAxis2 * (units::blocksInChunkDim-1)
						};
						auto const testNeighbourBlocksArea{ intersectAreas3i(
							{ neighbourAabb.start()         , neighbourAabb.end()           },
							{ firstInNeighbourChunkPos.val(), lastInNeighbourChunkPos.val() }
						) };
						
						for(int o1{0}; o1 < units::cubesInChunkDim; o1++)
						for(int o2{0}; o2 < units::cubesInChunkDim; o2++) {
							pCube const cubePosInChunk{
								axis   * (positive ? units::cubesInChunkDim-1 : 0)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							pCube const neighbourCubePosInNeighbourChunk{
								axis   * (positive ? 0 : units::cubesInChunkDim-1)
								+ oAxis1 * o1
								+ oAxis2 * o2
							};
							
							auto const neighbourCube{ [&]() {
								if(testNeighbourBlocksArea.contains(neighbourCubePosInNeighbourChunk.valAs<pBlock>())) {
									return neighbourChunkBlocks.cubeAt(neighbourCubePosInNeighbourChunk.val());   
								}
								else return chunk::ChunkData::Cube{};
							}() };
							
							if(BlocksLightingConfig::getType(neighbourCube.block.id(), neighbourCube.isSolid) != LightingCubeType::medium) continue;
							
							auto const &cubeLight   { chunkLighting         [cubePosInChunk                  .val()] };
							auto &neighbourCubeLight{ neighbourChunkLighting[neighbourCubePosInNeighbourChunk.val()] };
							
							auto const toNeighbourCubeLighting{ 
								BlocksLightingConfig::propagationRule(cubeLight, toNeighbourDir, neighbourCube.block.id(), neighbourCube.isSolid)
							};
							if(toNeighbourCubeLighting > neighbourCubeLight) {
								neighbourChunk.status().setUpdateLightingAdd(true);
								goto nextChunk3;
							}
						}
						nextChunk3:;
					}
				}
			}
		}
		else /*
			propagate borders lighting out
			is performed by AddLighting::fromCube, so we don't need to do anything
			if blockLightingInColumn is false
	    */;
	}
}