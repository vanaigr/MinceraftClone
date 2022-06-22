#pragma once

#include"Vector.h"
#include"Chunk.h"

#include<type_traits>

template<typename C, typename Action> 
inline std::enable_if_t<std::is_same_v< decltype(std::declval<Action>() ( std::declval<vec3<C>>() )), void >>
iterateAreaX(vec3<C> const begin, vec3<C> const end, Action &&action) {
	for(C z{begin.z}; z != end.z; z ++)
	for(C y{begin.y}; y != end.y; y ++)
	for(C x{begin.x}; x != end.x; x ++) {
		action(vec3<C>{x, y, z});
	}
}

template<typename C, typename Action> 
inline std::enable_if_t<std::is_same_v< decltype(std::declval<Action>() ( std::declval<vec3<C>>() )), void >>
iterateArea(vec3<C> const begin, vec3<C> const end, Action &&action) {
	for(C z{begin.z}; z <= end.z; ++z)
	for(C y{begin.y}; y <= end.y; ++y)
	for(C x{begin.x}; x <= end.x; ++x) {
		action(vec3<C>{x, y, z});
	}
}

template<typename T> struct Area {
	T first; T last; 
	
	bool isEmpty() const {
		return (last < first).any();
	}
	
	bool contains(T const value) const {
		if(isEmpty()) return false;
		else return value.clamp(first, last) == value;
	}
};
template<typename T> inline constexpr Area<vec3<T>> intersectAreas3(Area<vec3<T>> const a1, Area<vec3<T>> const a2) {
	auto const i = [](T const v1, T const v2, T const u1, T const u2) -> Area<T> {
		auto const vi{ misc::min(v1, v2) };
		auto const va{ misc::max(v1, v2) };
		auto const ui{ misc::min(u1, u2) };
		auto const ua{ misc::max(u1, u2) };
		
		//	 if(va <= ui) return { ui, va }; //empty
		//else if(ua <= vi) return { vi, ua }; //empty
		if(va < ui || ua < vi) return {0, -1};
		else return { misc::clamp(v1, u1, u2), misc::clamp(v2, u1, u2) };
	};
	
	if(a1.isEmpty()) return a1;
	if(a2.isEmpty()) return a2;
	
	Area<T> const is[] = { 
		i(a1.first.x, a1.last.x, a2.first.x, a2.last.x),
		i(a1.first.y, a1.last.y, a2.first.y, a2.last.y),
		i(a1.first.z, a1.last.z, a2.first.z, a2.last.z)
	};
	
	return {
		vec3<T>{ is[0].first, is[1].first, is[2].first },
		vec3<T>{ is[0].last , is[1].last , is[2].last  }
	};
}

inline constexpr Area<vec3i> intersectAreas3i(Area<vec3i> const a1, Area<vec3i> const a2) {
	return intersectAreas3<vec3i::value_type>(a1, a2);
}

/*static_assert(
	intersectAreas3i({vec3i{1, 2, 3}, vec3i{4, 5, 6}}, {vec3i{3, 1, 1}, vec3i{6, 4, 4}}).first == vec3i{ 3, 2, 3 } &&
	intersectAreas3i({vec3i{1, 2, 3}, vec3i{4, 5, 6}}, {vec3i{3, 1, 1}, vec3i{6, 4, 4}}).last  == vec3i{ 4, 4, 4 }
);*/

struct Bounds {		
	vec3i first;
	vec3i last;
	
	constexpr bool isEmpty() const {
		return (last >= first).all();
	}
};

struct BlockBounds : Bounds {
	constexpr static BlockBounds oneChunk() {
		return BlockBounds{ vec3i{0}, vec3i{units::blocksInChunkDim - 1} };
	}
	
	constexpr static BlockBounds emptyChunk() {
		return BlockBounds{ vec3i{units::blocksInChunkDim - 1}, vec3i{0} };
	}
};

struct CubeBounds : Bounds {
	constexpr static CubeBounds oneChunk() {
		return CubeBounds{ vec3i{0}, vec3i{units::cubesInChunkDim - 1} };
	}
	
	constexpr static CubeBounds emptyChunk() {
		return CubeBounds{ vec3i{units::cubesInChunkDim - 1}, vec3i{0} };
	}
	
	constexpr static CubeBounds emptyLimits() {
		return CubeBounds{ std::numeric_limits<vec3i::value_type>::max(), std::numeric_limits<vec3i::value_type>::lowest() };
	}
};

//Bounds and Area are essentially the same

inline constexpr int index3FromDir(vec3i const dir) {
	assert((dir >= -1).all() && (dir <= 1).all());
	return (dir.x+1) + (dir.y+1)*3 + (dir.z+1) * 9;
}

inline constexpr vec3i dirFromIndex3(int const i) {
	assert(i >= 0 && i < 27);
	return { (i%3)-1, ((i/3)%3)-1, ((i/9)%3)-1 };
}

template<typename T> inline void iterate3by3Volume(T &&action) {
	for(int i{}; i < 27; i++) {
		vec3i const dir{ dirFromIndex3(i) };
		
		if constexpr(std::is_convertible_v<decltype(action( vec3i{}, int(0) )), bool>) {
			if(action(dir, i)) break;	
		}
		else {
			action(dir, i);
		}
	}
}

template<typename Action>
inline void iterateCubeNeighbours(
	chunk::Chunk cubeChunk, vec3i const cubeCoord, 
	Action &&action
) {
	auto &chunks{ cubeChunk.chunks() };
	auto const chunkCoord{ cubeChunk.position() };
	
	for(auto i{decltype(chunk::ChunkLighting::dirsCount){}}; i < chunk::ChunkLighting::dirsCount; i++) {
		auto const neighbourDir{ chunk::ChunkLighting::indexAsDir(i) };
		
		auto const neighbourCubePos{ pos::Chunk{chunkCoord} + pos::Cube{cubeCoord + neighbourDir} };
		auto const neighbourCubeChunkCoord{ neighbourCubePos.valAs<pos::Chunk>() };
		auto const neighbourCubeInChunkCoord{ neighbourCubePos.as<pos::Cube>().valIn<pos::Chunk>() };
		
		auto const neighbourCubeChunkIndex{ chunk::Move_to_neighbour_Chunk{cubeChunk}.move(neighbourCubeChunkCoord).get() };
		if(neighbourCubeChunkIndex == -1) continue;

		action(neighbourDir, chunks[neighbourCubeChunkIndex], neighbourCubeInChunkCoord);
	}
}

template<typename Action>
inline void iterateChunks(chunk::Chunk const startChunk, pChunk const first, pChunk const last, Action &&action) {
	auto const fChunk{ first.val() };
	auto const lChunk{ last .val() };
	chunk::MovingChunk zMTNChunk{ startChunk };
	
	for(auto cz{ fChunk.z }; cz <= lChunk.z; cz++) {
		zMTNChunk = zMTNChunk.moved({fChunk.x, fChunk.y, cz});
		if(!zMTNChunk.is()) continue;
		auto yMTNChunk{ zMTNChunk };
		
	for(auto cy{ fChunk.y }; cy <= lChunk.y; cy++) {
		yMTNChunk = yMTNChunk.moved({fChunk.x, cy, cz});
		if(!yMTNChunk.is()) continue;
		auto xMTNChunk{ zMTNChunk };
		
	for(auto cx{ fChunk.x }; cx <= lChunk.x; cx++) {
		xMTNChunk = xMTNChunk.moved({cx, cy, cz});
		if(!xMTNChunk.is()) continue;
		
		assert(xMTNChunk.get().position() == (vec3i{cx, cy, cz}));
		action(xMTNChunk.get(), vec3i{cx, cy, cz});
	}}}
}