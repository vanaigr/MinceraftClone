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
	chunk::ChunkLighting& (  *getLighting_      )(chunk::Chunk chunk);
	uint8_t&              (  *getLight_         )(chunk::Chunk chunk, vec3i const cubeInChunkCoord);
	LightingCubeType      (  *getType_          )(uint16_t const blockId, bool const cube);
	uint8_t               (  *propagationRule_  )(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube);

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
	
	LightingCubeType getType(uint16_t const blockId, bool const cube) const { return getType_(blockId, cube); }
	
	uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube) const { 
		return propagationRule_(lighting, fromDir, toBlockId, cube); 
	}
};


struct SkyLightingConfig {
	static chunk::ChunkLighting &getLighting(chunk::Chunk chunk) {
		return chunk.skyLighting();
	}
	static uint8_t &getLight(chunk::Chunk chunk, vec3i const cubeInChunkCoord) { 
		return getLighting(chunk)[cubeInChunkCoord]; 
	}
	
	static LightingCubeType getType(uint16_t const blockId, bool const cube) { 
		if(!cube || isBlockTranslucent(blockId)) return LightingCubeType::medium;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		
		int const loss{ lightingLost(toBlockId * cube) };
		
		return misc::max(
			int(lighting)
			 - (fromDir == vec3i{0,-1,0} ? 0 : cubeLightingLosses)
			 - loss, 
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
	
	static LightingCubeType getType(uint16_t const blockId, bool const cube) { 
		if(!cube || isBlockTranslucent(blockId)) return LightingCubeType::medium;
		else if(isBlockEmitter(blockId)) return LightingCubeType::emitter;
		else return LightingCubeType::wall;
	}
	
	static uint8_t propagationRule(uint8_t const lighting, vec3i const fromDir, uint16_t const toBlockId, bool const cube) {
		assert(chunk::ChunkLighting::checkDirValid(fromDir));
		
		int const loss{ lightingLost(toBlockId * cube) };
		
		return misc::max(
			int(lighting) - cubeLightingLosses - loss, 
			int(0)
		);
	}
};

template<typename... Configs>
inline void setNeighboursLightingUpdate(chunk::Chunks &chunks, vec3i const minChunkPos, vec3i const maxChunkPos) {
	static vec3i const sides[3]       = { {1,0,0}, {0,1,0}, {0,0,1} };
	static vec3i const otherSides1[3] = { {0,0,1}, {0,0,1}, {0,1,0} };
	static vec3i const otherSides2[3] = { {0,1,0}, {1,0,0}, {1,0,0} };
	
	for(int axisPositive{}; axisPositive < 2; axisPositive++) 
	for(int side{}; side < 3; side++) {
	   for(auto chunkCoord1{minChunkPos.dot(otherSides1[side])}; chunkCoord1 <= maxChunkPos.dot(otherSides1[side]); chunkCoord1++)
		for(auto chunkCoord2{minChunkPos.dot(otherSides2[side])}; chunkCoord2 <= maxChunkPos.dot(otherSides2[side]); chunkCoord2++) {
			auto const neighbourDir{ sides[side] * (axisPositive*2 - 1) };
			
			auto const chunkCoord{
				  (axisPositive ? maxChunkPos : minChunkPos) * sides[side]
				+ otherSides1[side] * chunkCoord1
				+ otherSides2[side] * chunkCoord2
			};
			auto const chunkIndex{ chunk::Move_to_neighbour_Chunk{chunks, chunkCoord}.optChunk().get() };
			if(chunkIndex == -1) continue;
			auto const chunk{ chunks[chunkIndex] };
			
			auto const neighbourChunkCoord{ chunkCoord + neighbourDir };
			auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunk}.move(neighbourChunkCoord).get() };	
			if(neighbourChunkIndex == -1) continue;
			auto neighbourChunk{ chunks[neighbourChunkIndex] };
			
			if(neighbourChunk.status().isUpdateLightingAdd()) continue; /*
				the chunk is already marked as needing lighting update,
				the code below doesn't modify chunk's lighting so we can skip it
			*/
			
			static constexpr ConfigInstance configs[]{ ConfigInstance::from<Configs>()... };
			auto const &neighbourBlocks{ neighbourChunk.data() };
			
			for(auto const &config : configs) {	
				for(int coord1{}; coord1 < units::cubesInChunkDim; coord1++)
				for(int coord2{}; coord2 < units::cubesInChunkDim; coord2++) {
					auto const cubeInChunkCoord{
						sides[side] * (axisPositive ? units::cubesInChunkDim-1 : 0)
						+ otherSides1[side] * coord1
						+ otherSides2[side] * coord2
					};
					
					auto const neighbourCubeInChunkCoord{
						sides[side] * (axisPositive ? 0 : units::cubesInChunkDim-1)
						+ otherSides1[side] * coord1
						+ otherSides2[side] * coord2
					};
					
					auto const neighbourCube{ neighbourBlocks.cubeAt(neighbourCubeInChunkCoord) };
					auto const neighbourBlockId{ neighbourCube.block.id() };
					auto const neighbourBlockCube{ neighbourCube.isSolid };
								
					auto const type{ config.getType(neighbourBlockId, neighbourBlockCube) };
					if(type != LightingCubeType::medium) continue;
					
					auto const cubeLight{ config.getLight(chunk, cubeInChunkCoord) };
					auto const toNeighbourCubeLightingLevel{ config.propagationRule(cubeLight, neighbourDir, neighbourBlockId, neighbourBlockCube) };
					if(toNeighbourCubeLightingLevel == 0) continue;
					
					auto &neighbourCubeLight{ config.getLight(neighbourChunk, neighbourCubeInChunkCoord) };
					if(toNeighbourCubeLightingLevel > neighbourCubeLight) {
						neighbourChunk.status().setUpdateLightingAdd(true);
						goto nextChunk;
					}
				}
			}
		   nextChunk:;
		}
	}
}

static /*constexpr*/ chunk::ChunkLighting const skyLighting{ chunk::ChunkLighting::maxValue };
	
template<typename Config>
inline void fastUpdateLightingInDir/*more like coarseUpdateLightingInDir*/(
	chunk::Chunk chunk, 
	vec3i const dir, 
	vec3i const otherAxis1, vec3i const otherAxis2, 
	CubeBounds const bounds,
	CubeBounds &newBounds
) {
	auto const fromCube{ bounds.first };
	auto const toCube{ bounds.last };
	
	auto const chunkIndex{ chunk.chunkIndex() };
	auto const chunkCoord{ chunk.position() };
	auto &chunks{ chunk.chunks() };
	auto &lighting{ Config::getLighting(chunk) };
	
	auto const axis{ dir.abs() };
	auto const dirPositive{ (dir > 0).any() };
	
	
	pCube const beginCoordsCubeAxisUnbound{
		fromCube.dot(axis),
		fromCube.dot(otherAxis1),
		fromCube.dot(otherAxis2)
	}; /*coords along specified axis, in cubes*/
	pCube const endCoordsCubeAxisUnbound{
		toCube.dot(axis),
		toCube.dot(otherAxis1),
		toCube.dot(otherAxis2)
	};
	

	pChunk const chunkCoordAxis{ vec3i(chunkCoord).dot(axis), vec3i(chunkCoord).dot(otherAxis1), vec3i(chunkCoord).dot(otherAxis2) };/*
		chunk coords alond specified axis, in chunks
	*/

	auto const beginInChunkCoordsAxis{ (beginCoordsCubeAxisUnbound - chunkCoordAxis).valAs<pCube>().clamp(0, units::cubesInChunkDim - 1) };
	auto const endInChunkCoordsAxis  { (endCoordsCubeAxisUnbound   - chunkCoordAxis).valAs<pCube>().clamp(0, units::cubesInChunkDim - 1) };
	
	auto const dirCoordDiff{ endInChunkCoordsAxis.x - beginInChunkCoordsAxis.x };
	
	
	auto const startCubeInChunkCoord{
		  axis       * (dirPositive ? beginInChunkCoordsAxis.x : endInChunkCoordsAxis.x)
		+ otherAxis1 * beginInChunkCoordsAxis.y
		+ otherAxis2 * beginInChunkCoordsAxis.z
	};
	auto const beforeStartCubePos{ pChunk{chunkCoord} + pCube{startCubeInChunkCoord - dir} };
	auto const chunkBeforeIndex{ chunk::Move_to_neighbour_Chunk{chunk}.move(beforeStartCubePos.valAs<pChunk>()).get() };
	
	auto const isBefore{ chunkBeforeIndex != chunkIndex };
	
	auto const canUseSkyLighting{ std::is_same_v<SkyLightingConfig, Config> && dir == vec3i{0,-1,0} };/*
		this check is not preformed in other places where it should be preformed (for example in AddLighting::)
	*/
	
	CubeBounds updatedCubesBounds = CubeBounds::emptyLimits();
	
	auto const updateLayer = [&](
		auto FIRST_LAYER /*
			Templating on some type. For some reason, creating template instantiations
			for the first layer and for all the other layers is faster to run.
			It may be because actually only the first layer needs mod() to calculate cubeCoordBefore.
			Adding or remoing ternary operator in cubeCoordBefore's constructor doesn't change the total time
			but the operator is kept there just in case.
		*/, 
		int layerIndex, chunk::ChunkLighting &curChunkLighting, chunk::ChunkLighting const &lightingBefore
	) -> bool {
		bool layerLighting0{ true };/*if all cubes in this layer have no lighting*/
		
		for(auto o1{beginInChunkCoordsAxis.y}; o1 <= endInChunkCoordsAxis.y; o1++)
		for(auto o2{beginInChunkCoordsAxis.z}; o2 <= endInChunkCoordsAxis.z; o2++) {
			vec3i const cubeInChunkCoord(
				  axis       * (dirPositive ? beginInChunkCoordsAxis.x + layerIndex : endInChunkCoordsAxis.x - layerIndex)
				+ otherAxis1 * o1
				+ otherAxis2 * o2
			);
			
			auto const cube{ chunk.data().cubeAt(cubeInChunkCoord) };
			auto const blockId{ cube.block.id() };
			auto const isCube{ cube.isSolid };
			
			auto const type{ Config::getType(blockId, isCube) };
			if(type != LightingCubeType::medium) continue;
			auto const cubeCoordBefore{
				decltype(FIRST_LAYER)::value 
				? (cubeInChunkCoord - dir).mod(units::cubesInChunkDim) 
				: (cubeInChunkCoord - dir)
			};
			
			auto const candLighting{ Config::propagationRule(lightingBefore[cubeCoordBefore], dir, blockId, isCube) };
			auto &cubeLighting{ lighting[cubeInChunkCoord] };
			
			auto const updateLight{ cubeLighting < candLighting && candLighting > 0 };
			if(updateLight) {
				cubeLighting = candLighting;
				updatedCubesBounds.first = updatedCubesBounds.first.min(cubeInChunkCoord);
				updatedCubesBounds.last  = updatedCubesBounds.last .max(cubeInChunkCoord);
			}
			
			layerLighting0 = layerLighting0 && cubeLighting == 0;
		}
		
		return layerLighting0;
	};
	
	bool prevLevelAll0;
	
	if((canUseSkyLighting || chunkBeforeIndex != -1) && isBefore) {
		auto &lightingBefore{ 
			chunkBeforeIndex == -1 /*=> canUseSkyLighting (the opposite is not always true)*/ ? 
			  skyLighting 
			: Config::getLighting(chunks[chunkBeforeIndex])
		};
		
		prevLevelAll0 = updateLayer(std::true_type{}, 0, lighting, lightingBefore);
	} 
	else prevLevelAll0 = true;

	for(auto i{int(isBefore) + prevLevelAll0}; i <= dirCoordDiff; i += 1 + int(prevLevelAll0)) {
		prevLevelAll0 = updateLayer(std::false_type{}, i, lighting, lighting);
	}
	
	if(!updatedCubesBounds.isEmpty()) {
		newBounds.first = newBounds.first.min(pChunk{chunkCoord}.valAs<pCube>() + updatedCubesBounds.first);
		newBounds.last  = newBounds.last .max(pChunk{chunkCoord}.valAs<pCube>() + updatedCubesBounds.last );
	}
}

/*static void addChunkIndicesInArea(std::vector<int> &chunkIndices, pChunk const fromChunk, pChunk const toChunk, chunk::Chunks const &chunks) {
	//static std::vector<int> chunkIndices{};
	//chunkIndices.clear();
	
	vec3i const dir( (toChunk - fromChunk).valAs<pChunk>() > 0 );
	
	for(int x{ minChunkPos.x }; x <= maxChunkPos.x; x+= dir.z)
	for(int z{ minChunkPos.z }; z <= maxChunkPos.z; z+= dir.y)
	for(int y{ minChunkPos.y }; y <= maxChunkPos.y; y+= dir.x) {
		vec3i const curChunkPos{ x, y, z };
		auto const neighbourChunkIndex{ chunk::Move_to_neighbour_Chunk{chunks}.move(curChunkPos).get() };
		
		if(neighbourChunkIndex != -1) {
			chunkIndices.push_back(neighbourChunkIndex);
			
			auto curChunk{ chunks[neighbourChunkIndex] };
			curChunk.status().setLightingUpdated(true);
			
			auto &blockLighting{ curChunk.blockLighting() };
			for(chunk::ChunkBlocksList::value_type const blockIndex : curChunk.emitters()) {
				auto const blockInChunkCoord{ chunk::indexBlock(blockIndex) };
				for(int cubeInBlockIndex{}; cubeInBlockIndex < pos::cubesInBlockCount; cubeInBlockIndex++) {
					auto const cubeInBlockCoord{ chunk::Block::cubeIndexPos(cubeInBlockIndex) };
					
					auto const cubeInChunkCoord{ blockInChunkCoord*units::cubesInBlockDim + cubeInBlockCoord };
					
					blockLighting[cubeInChunkCoord] = chunk::ChunkLighting::maxValue;
				}
			}
		}
	}
}*/

inline void fillEmittersBlockLighting(chunk::Chunk chunk) {
	auto &blockLighting{ chunk.blockLighting() };
	
	for(chunk::ChunkBlocksList::value_type const blockIndex : chunk.emitters()) {
		pos::Block const blockInChunkCoord{ chunk::indexBlock(blockIndex) };
		for(int cubeInBlockIndex{}; cubeInBlockIndex < pos::cubesInBlockCount; cubeInBlockIndex++) {
			pos::Cube const cubeInBlockCoord{ chunk::Block::cubeIndexPos(cubeInBlockIndex) };
			
			auto const cubeInChunkCoord{ (blockInChunkCoord + cubeInBlockCoord).valAs<pos::Cube>() };
			
			blockLighting[cubeInChunkCoord] = chunk::ChunkLighting::maxValue;
		}
	}
}

template<typename Config>
inline void updateLightingInChunks(chunk::Chunks &chunks, pChunk const fromChunk, pChunk const toChunk) {
	CubeBounds cubesBounds{ fromChunk.valAs<pCube>(), toChunk.valAs<pCube>() + units::cubesInChunkDim-1 };
	
	for(int iters{}; iters < 10; iters++) {
		CubeBounds newCubesBounds = CubeBounds::emptyLimits();
		
		for(int dirIndex{}; dirIndex < 2; dirIndex++) {
			auto const dir{ dirIndex*2 - 1 };
			
			for(int i{}; i < 3; i++) {
				static pChunk::value_type const updateAxiss[] = {
					{0,1,0}, //propagate downwards first (for now even for block lighting)
					{1,0,0},
					{0,0,1},
				};
				static pChunk::value_type const otherAxiss1[] = { {0,0,1}, {0,0,1}, {0,1,0} };
				static pChunk::value_type const otherAxiss2[] = { {1,0,0}, {0,1,0}, {1,0,0} };
				
				auto const otherAxis1{ otherAxiss1[i] };
				auto const otherAxis2{ otherAxiss2[i] };
				auto const updateAxis{ updateAxiss[i] };
				auto const updateDir{ updateAxis * dir };
					
				auto updateDirChunksiff{ (toChunk - fromChunk).valAs<pChunk>().dot(updateAxis) };
				auto updateDirStart{ (dirIndex == 1 ? fromChunk : toChunk).valAs<pChunk>().dot(updateAxis) };
				
				for(uChunk::value_type o1{fromChunk.val().dot(otherAxis1)}; o1 <= toChunk.val().dot(otherAxis1); o1++)
				for(uChunk::value_type o2{fromChunk.val().dot(otherAxis2)}; o2 <= toChunk.val().dot(otherAxis2); o2++) 
				for(uChunk::value_type a {0}; a <= updateDirChunksiff; a ++) {
					pChunk const chunkPos{ 
						  otherAxis1 * o1
						+ otherAxis2 * o2
						+ updateAxis * ( updateDirStart + dir * a )
					};

					auto const curChunkIndex{ chunk::Move_to_neighbour_Chunk{ chunks }.move(chunkPos.val()).get() };
					if(curChunkIndex == -1) continue;
					
					auto curChunk{ chunks[curChunkIndex] };
					
					fastUpdateLightingInDir<Config>(
						curChunk,
						updateDir, 
						otherAxis1, otherAxis2,
						cubesBounds, 
						*&newCubesBounds
					);
				}
			}
		}
		
		if(newCubesBounds.isEmpty()) return;
		else {
			cubesBounds = newCubesBounds;
		}
	}

	auto lastChunkIndex{ -1 };

	for(uChunk::value_type z{fromChunk.val().z}; z <= toChunk.val().z; z++) 
	for(uChunk::value_type y{fromChunk.val().y}; y <= toChunk.val().y; y++) 
	for(uChunk::value_type x{fromChunk.val().x}; x <= toChunk.val().x; x++) {
		pChunk::value_type const chunkCoord{ x, y, z };
		lastChunkIndex = chunk::Move_to_neighbour_Chunk{chunks, chunk::OptionalChunkIndex{lastChunkIndex}}.move(chunkCoord).get();
		if(lastChunkIndex == -1) continue;
		auto chunk{ chunks[lastChunkIndex] };
		
		for(int i{}; i < pos::cubesInChunkCount; i++) {
			AddLighting::fromCube<Config>(chunk, chunk::cubeCoordInChunk(i));
		}
	}
}


void calculateLighting(chunk::Chunks &chunks, int (&chunkIndices)[32], 
vec2i const columnPosition, int const lowestNotFullY, int const highestNotEmptyY, int const lowestEmptyY);