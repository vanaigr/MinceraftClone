#include"Physics.h"

static bool checkCanPlaceCollider(chunk::Chunks &chunks, pos::Fractional const coord1, pos::Fractional const coord2) {
	chunk::Move_to_neighbour_Chunk mtnChunk{chunks, coord1.valAs<pos::Chunk>() };
	
	auto const c1c{ coord1.valAs<pos::Cube>() };
	auto const c2c{ coord2.valAs<pos::Cube>() };
	
	for(auto z{ c1c.z }; z <= c2c.z; z++) 
	for(auto y{ c1c.y }; y <= c2c.y; y++)
	for(auto x{ c1c.x }; x <= c2c.x; x++) {
		pos::Cube cubePos{{x, y, z}};
		
		auto const chunkIndex{ mtnChunk.move(cubePos.valAs<pos::Chunk>()).get() };
		if(chunkIndex == -1) return false;
		
		auto cube{ chunks[chunkIndex].data().cubeAt(cubePos.valIn<pos::Chunk>()) };
		if(cube.isSolid) return false;
	}
	
	return true;
}

//https://www.mcpk.wiki/wiki/Collisions
void updateCollision(chunk::Chunks &chunks,pos::Fractional &origin, pFrac const offsetMin, pFrac const offsetMax, vec3d &forceF, bool &isOnGround) {
	//note: with this collision resolution it is possible to get stuck outside of the chunks if the player was spawned there
	
	auto position{ origin.val() };
	auto force{ pos::posToFracTrunk(forceF).val() };
	
	vec3l maxPosition{};
	
	vec3i dir{};
	vec3b positive_{};
	vec3b negative_{};
	
	vec3l playerMin{};
	vec3l playerMax{};
	
	//define cubes that are checked for collision
	vec3i min{};
	vec3i max{};
	
	chunk::Move_to_neighbour_Chunk chunk{ chunks, origin.valAs<pos::Chunk>() };
	
	auto const offset = [&]() { return positive_.select(*offsetMin, *offsetMax); };
	
	auto const updateBounds = [&]() {			
		dir = force.sign();
		positive_ = dir > vec3i(0);
		negative_ = dir < vec3i(0);
		
		maxPosition = position + force;
		
		playerMin = position + offsetMin.val();
		playerMax = position + offsetMax.val();
		
		min = pos::Fractional(playerMin    ).valAs<pos::Cube>();
		max = pos::Fractional(playerMax - 1).valAs<pos::Cube>();
	};
	
	struct MovementResult {
		pos::Fractional coord;
		bool isCollision;
		bool continueMovement;
	};
	
	auto const moveAlong = [&](vec3b const axis, int64_t const axisPlayerOffset, vec3b const otherAxis1, vec3b const otherAxis2, bool const upStep) -> MovementResult {	
		assert((otherAxis1 || otherAxis2).equal(!axis).all());
		
		auto const otherAxis{ otherAxis1 || otherAxis2 };
		
		auto const axisPositive{ positive_.dot(axis) };
		auto const axisNegative{ negative_.dot(axis) };
		auto const axisDir{ dir.dot(vec3i(axis)) };
		
		auto const axisPlayerMax{ playerMax.dot(vec3l(axis)) };
		auto const axisPlayerMin{ playerMin.dot(vec3l(axis)) };
		
		auto const axisPlayerPos{ position.dot(vec3l(axis)) };
		auto const axisPlayerMaxPos{ maxPosition.dot(vec3l(axis)) };
		
		auto const start{ units::Fractional{ axisPositive ? axisPlayerMax : axisPlayerMin }.valAs<units::Cube>() - axisNegative };
		auto const end{ units::Fractional{ axisPlayerMaxPos + axisPlayerOffset }.valAs<units::Cube>() };
		auto const count{ (end - start) * axisDir };
		
		
		for(int32_t a{}; a <= count; a++) {
			auto const axisCurCubeCoord{ start + a * axisDir };
			
			auto const axisNewCoord{ units::Cube{axisCurCubeCoord + axisNegative}.valAs<units::Fractional>() - axisPlayerOffset }; 
			pos::Fractional const newPos{ vec3l(axisNewCoord) * vec3l(axis) + vec3l(position) * vec3l(otherAxis) };
				
			vec3l const upStepOffset{ vec3l{0, units::fracInCubeDim, 0} + vec3l(axis) * vec3l(axisDir) };
			pos::Fractional const upStepCoord{newPos + pos::Fractional{upStepOffset}};
			
			auto const upStepMin{ upStepCoord.val() + offsetMin     };
			auto const upStepMax{ upStepCoord.val() + offsetMax - 1 };
			
			auto const upStepPossible{ upStep && checkCanPlaceCollider(chunks, upStepMin, upStepMax)};
			
			auto const newCoordBigger{
				axisPositive ? (axisNewCoord >= axisPlayerPos) : (axisNewCoord <= axisPlayerPos)
			};
			
			for(auto o1{ min.dot(vec3i(otherAxis1)) }; o1 <= max.dot(vec3i(otherAxis1)); o1++)
			for(auto o2{ min.dot(vec3i(otherAxis2)) }; o2 <= max.dot(vec3i(otherAxis2)); o2++) {
				vec3i const cubeCoord{
					  vec3i(axis      ) * axisCurCubeCoord
					+ vec3i(otherAxis1) * o1
					+ vec3i(otherAxis2) * o2
				};
				
				auto const cubeLocalCoord{ 
					cubeCoord.applied([](auto const coord, auto i) -> int32_t {
						return int32_t(misc::mod<int64_t>(coord, units::cubesInBlockDim));
					})
				};
				
				pos::Fractional const coord{ pos::Cube(cubeCoord) };
						
				vec3i const blockChunk = coord.valAs<pos::Chunk>();
				
				auto const isWall{ [&]() {
					auto const chunkIndex{ chunk.move(blockChunk).get() };
					if(chunkIndex == -1) return true;
					
					auto const chunkData{ chunks.chunksData[chunkIndex] };
					auto const index{ chunk::blockIndex(coord.as<pos::Block>().valIn<pos::Chunk>()) };
					auto const block{ chunkData[index] };
					
					return block.id() != 0 && block.cube(cubeLocalCoord);
				}() };
				
				if(isWall) {
					if(newCoordBigger) {
						auto const diff{ position.y - coord.value().y };
						if(upStepPossible && diff <= units::fracInCubeDim && diff >= 0) return { upStepCoord, false, true };
						else return { newPos, true, false };
					}
				}
			}
		}
		
		return { vec3l(axisPlayerMaxPos) * vec3l(axis) + vec3l(position) * vec3l(otherAxis), false, false };
	};
	
	updateBounds();
	
	if(dir.y != 0) {
		MovementResult result;
		do {
			result = moveAlong(vec3b{0,1,0}, offset().y, vec3b{0,0,1},vec3b{1,0,0}, false);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - position) * vec3l{0,1,0};
			}
			else if(result.isCollision) {
				if(negative_.y) isOnGround = true;
				force = pos::posToFracTrunk(pos::fracToPos(force) * 0.8).value();
				force.y = 0;
			}
			
			position = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}

	if(dir.x != 0) {
		MovementResult result;
		do { 
			result = moveAlong(vec3b{1,0,0}, offset().x, vec3b{0,0,1},vec3b{0,1,0}, isOnGround);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - position) * vec3l{1,0,0};
			}
			else if(result.isCollision) { force.x = 0; }
			
			position = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}
	
	if(dir.z != 0) {
		MovementResult result;
		do { 
			result = moveAlong(vec3b{0,0,1}, offset().z, vec3b{0,1,0},vec3b{1,0,0}, isOnGround);
			
			if(result.continueMovement) {
				force -= (result.coord.value() - position) * vec3l{0,0,1};
			}
			else if(result.isCollision) { force.z = 0; }
			
			position = result.coord.value();
			updateBounds();
		} while(result.continueMovement);
	}
	
	origin = pos::Fractional{ position };
	forceF = pos::fracToPos(force) * (isOnGround ? vec3d{0.8,1,0.8} : vec3d{1});
}