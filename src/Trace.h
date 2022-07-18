#pragma once

#include"Vector.h"
#include"Units.h"
#include"Position.h"
#include"Chunk.h"

#include<optional>

struct PosDir {
	vec3l start;
	vec3l end;
	
	vec3i direction;
	
	PosDir(pos::Fractional const coord, vec3l const line): 
		start{ coord.val() },
		end{ start + line },
		
		direction{ line.sign() }
	{} 
	
	constexpr vec3l::value_type atCoord(vec3i const inAxis_, vec3l::value_type coord, vec3i const outAxis_) const {
		vec3l const inAxis( inAxis_ );
		vec3l const outAxis( outAxis_ );
		auto const ist = start.dot(inAxis);
		auto const ind = end.dot(inAxis); 
		auto const ost = start.dot(outAxis); 
		auto const ond = end.dot(outAxis); 
		return ist == ind ? ost : ( ost + (ond - ost)*(coord-ist) / (ind - ist) );		
	};
	
	int difference(vec3l const p1, vec3l const p2) const {
		vec3l const diff{p1 - p2};
		return (diff * vec3l(direction)).sign().dot(1);
	};
	
	constexpr vec3l at(vec3i inAxis, vec3l::value_type const coord) const { 
		return vec3l{
			atCoord(inAxis, coord, vec3i{1,0,0}),
			atCoord(inAxis, coord, vec3i{0,1,0}),
			atCoord(inAxis, coord, vec3i{0,0,1})
		};
	}

	friend std::ostream &operator<<(std::ostream &o, PosDir const v) {
		return o << "PosDir{" << v.start << ", " << v.end << ", " << v.direction << "}";
	}
};

struct DDA {
public:
	struct State {
		pFrac coord;
		vec3b intersectionAxis;
		bool end;
	};
private:
	PosDir posDir;
	
	State state;
public:
	DDA(PosDir const pd_) : 
	    posDir{ pd_ },
		state{ posDir.start, posDir.start.equal((posDir.start >> units::fracInCubeDimAsPow2) << units::fracInCubeDimAsPow2), posDir.start == posDir.end }
	{}
	
	State get() const {
		return state; 
	}
	
	void next() {
		if(state.end) return;

		struct Candidate { vec3l coord; vec3b side; };
		
		vec3l const nextC{ nextCoords(state.coord.val(), posDir.direction) };
		
		Candidate const candidates[] = {
			Candidate{ posDir.at(vec3i{1,0,0}, nextC.x), vec3b{1,0,0} },
			Candidate{ posDir.at(vec3i{0,1,0}, nextC.y), vec3b{0,1,0} },
			Candidate{ posDir.at(vec3i{0,0,1}, nextC.z), vec3b{0,0,1} },
			Candidate{ posDir.end, 0 }//index == 3
		};
		
		int minI{ 3 };
		Candidate minCand{ candidates[minI] };
		
		for(int i{}; i < 4; i++) {
			auto const cand{ candidates[i] };
			auto const diff{ posDir.difference(cand.coord, minCand.coord) };
			if(posDir.difference(cand.coord, state.coord.val()) > 0 && diff <= 0) {
				minI = i;
				if(diff == 0) {
					minCand = Candidate{ 
						minCand.coord * vec3l(!vec3b(cand.side)) + cand.coord * vec3l(cand.side),
						minCand.side || cand.side
					};
				}
				else {
					minCand = cand;
				}
			}
		}
		
		state = State{ pFrac{minCand.coord}, minCand.side, minI == 3 };
	}
	
	static constexpr vec3l nextCoords(vec3l const current, vec3i const dir) {
		return current.applied([&](auto const coord, auto const i) -> int64_t {
			if(dir[i] >= 0) //round down
				return ((coord >> units::fracInCubeDimAsPow2) + 1) << units::fracInCubeDimAsPow2;
			else //round up
				return (-((-coord) >> units::fracInCubeDimAsPow2) - 1) << units::fracInCubeDimAsPow2;
		});
	};
};

struct Intersection {
	pCube cubePos;
	chunk::Chunk chunk;
	pCube cubeInChunkCoord;
	vec3b intersectionAxis;
};

template<typename StopAtBlock>
inline std::optional<Intersection> trace(chunk::Chunks &chunks, PosDir const pd, StopAtBlock &&stopAtBlock) {
	DDA checkBlock{ pd };
	
	for(int i = 0;; i++) {
		DDA::State const intersection{ checkBlock.get() };
		auto const intersectionAxis{ intersection.intersectionAxis };
		
		pos::Cube const cubePos{
			intersection.coord.as<pos::Cube>()
			+ pCube{pd.direction.min(0) * vec3i{intersectionAxis}}
		};
		auto const cubeCoordInChunk{ cubePos.in<pos::Chunk>() };
		
		auto const cubeLocalCoord{ 
			cubePos.value().applied([](auto const coord, auto i) -> int32_t {
				return int32_t(misc::mod<int64_t>(coord, units::cubesInBlockDim));
			})
		};
		
		auto const chunkIndex{ chunk::MovingChunk{chunks}.moved(cubePos.valAs<pos::Chunk>()).getIndex() };
		if(!chunkIndex.is()) break;
		
		auto const chunk{ chunks[chunkIndex.get()] };
		
		if(stopAtBlock(chunk.data().cubeAt2(cubeCoordInChunk))) return { Intersection{ 
			cubePos,
			chunk, 
			cubeCoordInChunk, 
			intersectionAxis 
		} };
		
		if(i >= 10000) {
			std::cout << __FILE__ << ':' << __LINE__ << " error: to many iterations!" << pd << '\n'; 
			break;
		}
		if(intersection.end) break;
		
		checkBlock.next();
	}

	return {};
}

/*struct BlockIntersection {
	chunk::Chunk chunk;
	int16_t blockIndex;
	uint8_t cubeIndex;
	vec3i intersectionAxis;
};

static std::optional<BlockIntersection> trace(chunk::Chunks &chunks, PosDir const pd) {
	DDA checkBlock{ pd };
		
	chunk::Move_to_neighbour_Chunk mtnChunk{ chunks, pd.chunk };
		
	vec3i intersectionAxis{0};
	for(int i = 0;; i++) {
		vec3l const intersection{ checkBlock.get_current() };
		
		pos::Cube const cubePos{
			pos::Chunk{ pd.chunk }.valAs<pos::Cube>()
			+ pos::Fractional(intersection).valAs<pos::Cube>()
			+ pd.direction.min(0) * intersectionAxis
		};
		
		auto const cubeLocalCoord{ 
			cubePos.value().applied([](auto const coord, auto i) -> int32_t {
				return int32_t(misc::mod<int64_t>(coord, units::cubesInBlockDim));
			})
		};
		
		auto const chunkIndex{ mtnChunk.move(cubePos.valAs<pos::Chunk>()).get() };
		if(chunkIndex == -1) break;
		
		auto const blockInChunkCoord{ cubePos.as<pos::Block>().valIn<pos::Chunk>() };
		auto const blockIndex{ chunk::blockIndex(blockInChunkCoord) };
		
		auto const chunk{ chunks[chunkIndex] };
		if(chunk.data().cubeAt(cubePos.valIn<pos::Chunk>()).isSolid) return { BlockIntersection{ 
			chunk, 
			blockIndex, 
			chunk::Block::cubePosIndex(cubeLocalCoord), 
			intersectionAxis 
		} };
		
		if(i >= 10000) {
			std::cout << __FILE__ << ':' << __LINE__ << " error: to many iterations!" << pd << '\n'; 
			break;
		}
		
		if(checkBlock.get_end()) {
			break;
		}
		
		intersectionAxis = vec3i(checkBlock.next());
	}

	return {};
}*/