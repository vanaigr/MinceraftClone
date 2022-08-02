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
			auto const cube{ cubeChunk.data().cubeAt2(pCube{cubeInChunkCoord}) };
			
			auto const type{ Config::getType(cube) };
			if(type != LightingCubeType::medium) return;
			
			auto &cubeLight{ Config::getLight(cubeChunk, cubeInChunkCoord) };
			auto const expectedLight{ Config::propagationRule(startLight, fromDir, cube) };
			if(expectedLight > cubeLight) {
				cubeLight = expectedLight;
				propagateAddLightInColumn<Config>(chunks, chunkIndices, y, cubeInChunkCoord, cubeLight);	
			}
			
		}
	);
}

template<typename Action>
void iterateFourNeighbours( //4 horisontal neighbours
	chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], 
	int const highY, int const lowY, 
	Action &&action
) {
	static constexpr vec3i updateSides[] = { {1, 0, 0}, {0, 0, 1} };
	static constexpr vec3i otherAxiss [] = { {0, 0, 1}, {1, 0, 0} };
	
	assert(highY <= chunkColumnChunkYMax && lowY >= chunkColumnChunkYMin);
	
	for(int chunkY{ highY }; chunkY >= lowY; chunkY--) {
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
				
				action(
					chunkI, chunk, neighbourChunk, 
					mat3i{ axis, oAxis1, oAxis2 }, positive, axisIndex + positive_ * 2
				);
			}
		}
	}
}

template<typename Action>
void iterateChunkFace(mat3i const axis, bool const positive, Action &&action) {
	for(int o1{0}; o1 < units::cubesInChunkDim; o1++)
	for(int o2{0}; o2 < units::cubesInChunkDim; o2++) {
		pCube const cubeCoord         { axis * vec3i{positive ? units::cubesInChunkDim-1 : 0  , o1, o2} };
		pCube const neighbourCubeCoord{ axis * vec3i{positive ? 0 : (units::cubesInChunkDim-1), o1, o2} };
		action(cubeCoord, neighbourCubeCoord);
	}
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
	
	iterateFourNeighbours(
		chunks, chunkIndices, 
		highestChunkY, lowestChunkY, 
		[&](
			int const chunkI, chunk::Chunk const chunk, chunk::Chunk const neighbourChunk, 
			mat3i const axis, bool const positive, int const axisIndex
		) {
			auto const fromNeighbourDir{ axis * vec3i{-1 * misc::positiveSign(positive), 0, 0} };
			auto const &chunkBlocks{ chunk.data() };
			auto &chunkLighting{ lightingConfig.getLighting(chunk) };	
			auto &neighbourChunkLighting{ lightingConfig.getLighting(neighbourChunk) };	
			
			auto const &aabb{ chunk.aabb() };
			auto const fBlock{ positive ? units::blocksInChunkDim-1 : 0 };
			
			auto const testBlocksArea{ intersectAreas3i(
				aabb, 
				{ axis * vec3i{fBlock, 0, 0}, 
				  axis * vec3i{fBlock, units::blocksInChunkDim-1, units::blocksInChunkDim-1} }
			) };
			
			iterateChunkFace(axis, positive, [&](pCube const cubeCoord, pCube const neighbourCubeCoord) {
				auto const chunkCube{ [&]() {
					if(testBlocksArea.contains(cubeCoord.valAs<pBlock>())) 
						 return chunkBlocks.cubeAt2(cubeCoord);   
					else return chunk::Block::id_t{};
				}() };
				
				if(lightingConfig.getType(chunkCube) != LightingCubeType::medium) return;
				
				auto &cubeLight               { chunkLighting         [cubeCoord         .val()] };
				auto const &neighbourCubeLight{ neighbourChunkLighting[neighbourCubeCoord.val()] };
				
				auto const toChunkCubeLighting{ 
					lightingConfig.propagationRule(neighbourCubeLight, fromNeighbourDir, chunkCube)
				};
				if(toChunkCubeLighting > cubeLight) {
					cubeLight = toChunkCubeLighting;
					(*updatedChunks)[chunkI][axisIndex] = true;
				}
			});
		}
	);
}

void calculateLighting(
	chunk::Chunks &chunks, int (&chunkIndices)[chunksCoumnChunksCount], vec2i const columnPosition, 
	int const lowestEmptyY
) {
	int const lowestNotFullY{ chunkColumnChunkYMin };
	
	{//sky lighting
		static std::vector<chunk::CubeInChunkIndex> lightingUpdateCubes{};
		lightingUpdateCubes.clear(); 
		auto const updateLightingCubes = [&](chunk::Chunk const chunk) {
			if(lightingUpdateCubes.empty()) return;
			chunk.status().current.lighting = false;
			for(auto const cubeIndex : lightingUpdateCubes) {
				AddLighting::fromCube<SkyLightingConfig>(chunk, chunk::cubeIndexToCoord(cubeIndex).val());
			}
			lightingUpdateCubes.clear();
		};
		
		{ //sky cuhnks
			//propagate lighting from empty chunks with maximum sky lighting to its 4 horisontal neighbours
			iterateFourNeighbours(
				chunks, chunkIndices, 
				chunkColumnChunkYMax, lowestEmptyY, 
				[&](
					int const chunkI, chunk::Chunk const chunk, chunk::Chunk const neighbourChunk, 
					mat3i const axis, bool const positive, int const axisIndex
				) {
					auto const toNeighbourDir{ axis * vec3i{ misc::positiveSign(positive), 0, 0 } };
					auto const &neighbourChunkBlocks{ neighbourChunk.data() };
					auto &neighbourChunkLighting{ SkyLightingConfig::getLighting(neighbourChunk) };
					
					auto const &neighbourAabb{ neighbourChunk.aabb() };
					auto const fBlock{ positive ? 0 : units::blocksInChunkDim-1 };
					auto const testNeighbourBlocksArea{ intersectAreas3i(
						neighbourAabb, 
						{
							axis * vec3i{fBlock, 0, 0}, 
							axis * vec3i{fBlock, units::blocksInChunkDim-1, units::blocksInChunkDim-1} 
						}
					) };
					
					iterateChunkFace(axis, positive, [&](pCube const cubeCoord, pCube const neighbourCubeCoord) {
						auto const toCube{ [&]() {
							if(testNeighbourBlocksArea.contains(neighbourCubeCoord.valAs<pBlock>())) {
								return neighbourChunkBlocks.cubeAt2(neighbourCubeCoord);   
							}
							else return chunk::Block::id_t{};
						}() };
						
						if(SkyLightingConfig::getType(toCube) != LightingCubeType::medium) return;
						
						auto const cubeLight    { chunk::ChunkLighting::maxValue };
						auto &neighbourCubeLight{ neighbourChunkLighting[neighbourCubeCoord] };
						
						auto const toNeighbourCubeLighting{ 
							SkyLightingConfig::propagationRule(cubeLight, toNeighbourDir, toCube)
						};
						if(toNeighbourCubeLighting > neighbourCubeLight) {
							neighbourCubeLight = toNeighbourCubeLighting;
							lightingUpdateCubes.push_back(chunk::cubeCoordToIndex(neighbourCubeCoord));
						}
					});
					
					updateLightingCubes(neighbourChunk);			
				}
			);
		}
		
		{ //downward lighting in chunks
			//move lighting downwards in non-empty chunks until the first opaque cube
			for(int chunkY{ lowestEmptyY-1 }; chunkY >= lowestNotFullY; chunkY--) {
				auto chunk{ chunks[chunkIndices[chunkY - chunkColumnChunkYMin]] };
				auto const aabb{ chunk.aabb() };
				
				auto &chunkLighting{ SkyLightingConfig::getLighting(chunk) };
				auto const &chunkLightingBefore{ [&](){
					if(chunkY == chunkColumnChunkYMax) return skyLighting;
					else return SkyLightingConfig::getLighting(chunks[chunkIndices[chunkY - chunkColumnChunkYMin + 1]]);
				}() }; //higher up
				
				auto const &chunkBlocks{ chunk.data() };
				
				vec2i layerLightingNot0First{ 0 };
				vec2i layerLightingNot0Last { units::cubesInChunkDim-1 };
				
				auto const updateLayer = [&](
					auto firstLayer_t/*templating on special case for better optimisation*/,
					int const y, int const yBefore, 
					chunk::ChunkLighting &lighting, chunk::ChunkLighting const &lightingBefore
				) -> bool {
					vec2i layerLightingNot0FirstNew{ units::cubesInChunkDim };
					vec2i layerLightingNot0LastNew { -1 };
					
					for(int z{layerLightingNot0First.y}; z <= layerLightingNot0Last.y; z++)
					for(int x{layerLightingNot0First.x}; x <= layerLightingNot0Last.x; x++) {
						pCube const cubePos      { x, y      , z };
						pCube const cubePosBefore{ x, yBefore, z };
						
						auto const cube{ [&]() {
							if(aabb.contains(cubePos.valAs<pBlock>())) return chunkBlocks.cubeAt2(cubePos.val());
							else return chunk::Block::id_t{};
						}() };
						
						if(SkyLightingConfig::getType(cube) != LightingCubeType::medium) continue;
						
						auto const cubeLighting{ chunk::ChunkLighting::minValue }; //chunks' lighting values were not set by anything yet, so they are 0
						auto const cubeBeforeLighting{ lightingBefore[cubePosBefore] };
						auto const toCubeLighting{ 
							SkyLightingConfig::propagationRule(cubeBeforeLighting, vec3i{0, -1, 0}, cube) 
						};
						if(toCubeLighting > cubeLighting) {
							lighting[cubePos] = toCubeLighting;
							layerLightingNot0FirstNew = layerLightingNot0FirstNew.min(vec2i{x, z});
							layerLightingNot0LastNew  = layerLightingNot0LastNew .max(vec2i{x, z});
						}
					}
					
					layerLightingNot0First = layerLightingNot0FirstNew;
					layerLightingNot0Last  = layerLightingNot0LastNew ;
					
					return (layerLightingNot0First <= layerLightingNot0Last).all(); 
				};
				
				auto continueProp{ false };
				//first layer
				continueProp = updateLayer(std::true_type{}, units::cubesInChunkDim-1, 0, chunkLighting, chunkLightingBefore);
				if(!continueProp) break;
				
				//other layers
				for(auto y{units::cubesInChunkDim-1 - 1}; y >= 0; y--) {
					continueProp = updateLayer(std::false_type{}, y, y+1, chunkLighting, chunkLighting);
					if(!continueProp) break;
				}
			}
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
			iterateFourNeighbours(
				chunks, chunkIndices, 
				lowestEmptyY-1, lowestNotFullY, 
				[&](
					int const chunkI, chunk::Chunk const chunk, chunk::Chunk const neighbourChunk, 
					mat3i const axis, bool const positive, int const axisIndex
				) {
					auto const toNeighbourDir{ axis * vec3i{ misc::positiveSign(positive), 0, 0 } };

					auto &chunkLighting{ SkyLightingConfig::getLighting(chunk) };	
					auto const &neighbourChunkBlocks{ neighbourChunk.data() };
					auto &neighbourChunkLighting{ SkyLightingConfig::getLighting(neighbourChunk) };
					
					auto const &neighbourAabb{ neighbourChunk.aabb() };
					auto const fBlock{ positive ? 0 : units::blocksInChunkDim-1 };
					auto const testNeighbourBlocksArea{ intersectAreas3i(
						neighbourAabb,
						{
							axis * vec3i{fBlock, 0, 0}, 
							axis * vec3i{fBlock, units::blocksInChunkDim-1, units::blocksInChunkDim-1} 
						}
					) };
					
					iterateChunkFace(axis, positive, [&](pCube const cubeCoord, pCube const neighbourCubeCoord) {						
						auto const neighbourCube{ [&]() {
							if(testNeighbourBlocksArea.contains(neighbourCubeCoord.valAs<pBlock>())) {
								return neighbourChunkBlocks.cubeAt2(neighbourCubeCoord);   
							}
							else return chunk::Block::id_t{};
						}() };
						
						if(SkyLightingConfig::getType(neighbourCube) != LightingCubeType::medium) return;
						
						auto const &cubeLight   { chunkLighting         [cubeCoord         ] };
						auto &neighbourCubeLight{ neighbourChunkLighting[neighbourCubeCoord] };
						
						auto const toNeighbourCubeLighting{ 
							SkyLightingConfig::propagationRule(cubeLight, toNeighbourDir, neighbourCube)
						};
						if(toNeighbourCubeLighting > neighbourCubeLight) {
							neighbourCubeLight = toNeighbourCubeLighting;
							lightingUpdateCubes.push_back(chunk::cubeCoordToIndex(neighbourCubeCoord));
						}
					});
					
					updateLightingCubes(neighbourChunk);
				}
			);
		}
	}

	{//block lighting
		Sides invalidated[chunksCoumnChunksCount] = {};
		
		//setup emitters
		for(int chunkY{chunkColumnChunkYMax}; chunkY >= lowestNotFullY; chunkY--) {
			auto const chunkI{ chunkY - chunkColumnChunkYMin };
			auto chunk{ chunks[chunkIndices[chunkI]] };
			fillEmittersBlockLighting(chunk);
		}
		
		//propagate borders lighting in
		propagateHorisontalLightingIn(
			chunks, chunkIndices, 
			chunkColumnChunkYMax, lowestNotFullY, 
			false,
			&invalidated
		);
		
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
							
							AddLighting::fromCube<BlocksLightingConfig>(chunk, cubePos.valAs<pCube>());
						}
					}
				}
			}
		}
		
		//propagate borders lighting out is performed by AddLighting::fromCube
	}
}