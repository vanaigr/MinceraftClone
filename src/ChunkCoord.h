#pragma once
#include"Vector.h"
#include<tuple>
#include<iostream>
#include<cmath>

struct ChunkCoord {	
	static constexpr auto fracChunkDimAsPow2 = 32;
	static constexpr auto fracChunkDim = 1ll << fracChunkDimAsPow2;
	static constexpr auto fracBlockDimAsPow2 = (32 - Chunks::chunkDimAsPow2);
	static constexpr auto fracBlockDim = 1ll << fracBlockDimAsPow2;
	static constexpr auto fracCubeDimAsPow2 = ChunkCoord::fracBlockDimAsPow2 - 1;
	static constexpr auto fracCubeDim = 1ll << fracCubeDimAsPow2;
	
	static_assert(fracBlockDim%2 == 0 && Chunks::chunkDimAsPow2 >= 0 && Chunks::chunkDimAsPow2 <= 32);
	
	static constexpr inline vec3l posToFrac(vec3d value) { return vec3l{(value*fracBlockDim).floor()}; }
	static constexpr inline vec3l posToFracTrunk(vec3d value) { return vec3l{(value*fracBlockDim).trunc()}; }
	static constexpr inline vec3l posToFracRAway(vec3d value) {  //round away from zero
		auto const val{ (value*fracBlockDim) };
		return vec3l{ val.abs().ceil().applied([&](auto const coord, auto i) -> double {
			return std::copysign(coord, val[i]);
		}) }; 
	}
	template<template<typename> typename Cont>
	static constexpr inline Cont<int64_t> blockToFrac(Cont<int64_t> value) { return Cont<int64_t>(value)*fracBlockDim; }
	template<template<typename> typename Cont>
	static constexpr inline Cont<int64_t> blockCubeToFrac(Cont<int32_t> value) { return Cont<int64_t>(value)*fracCubeDim; }
	static constexpr inline vec3l chunkToFrac(vec3i value) { return vec3l(value) * fracChunkDim; }
	static constexpr inline vec3l cubeToFrac(vec3l value) { return value * fracCubeDim; }
	
	static constexpr inline vec3l chunkToBlock(vec3i value) { return vec3l(value) * Chunks::chunkDim; }
	
	static constexpr inline vec3d fracToPos(vec3l value) { return static_cast<vec3d>(value) / fracBlockDim; }
	static constexpr inline vec3l fracToBlock(vec3l value) { return value >> fracBlockDimAsPow2; }
	static constexpr inline vec3i fracTochunk(vec3l value) { return vec3i(value >> fracChunkDimAsPow2); }
	static constexpr inline vec3l fracToBlockCube(vec3l value) { return value >> fracCubeDimAsPow2; }
	
	
	vec3l coordinate;
	
	struct Fractional { vec3l it; explicit Fractional(vec3l t) : it{t} {} };
	struct Cube       { vec3l it; explicit Cube      (vec3l t) : it{t} {} };
	struct Block      { vec3l it; explicit Block     (vec3l t) : it{t} {} };
	struct Position   { vec3d it; explicit Position  (vec3d t) : it{t} {} };
	struct Chunk      { vec3i it; explicit Chunk     (vec3i t) : it{t} {} };
		
	ChunkCoord() = default;
	ChunkCoord(vec3l const coord) : coordinate{ coord } {}
	ChunkCoord(Chunk const chunk) : coordinate{ chunkToFrac(chunk.it) } {}
	ChunkCoord(vec3i const chunk, Fractional const coord   ) : ChunkCoord{vec3l{chunk} * fracChunkDim + coord.it} {}
	ChunkCoord(vec3i const chunk, Position   const position) : ChunkCoord{chunk, Fractional{posToFrac(position.it)}} {}
	ChunkCoord(vec3i const chunk, Cube       const cube    ) : ChunkCoord{chunk, Fractional{cubeToFrac(cube.it )}} {}
	ChunkCoord(vec3i const chunk, Block      const block   ) : ChunkCoord{chunk, Fractional{blockToFrac(block.it )}} {}

	
	ChunkCoord(ChunkCoord const &) = default;
	ChunkCoord(ChunkCoord &&) = default;
	
	ChunkCoord & operator=(ChunkCoord const &) = default;
	ChunkCoord & operator=(ChunkCoord &&) = default;
	
	constexpr inline vec3l coord()  const { return coordinate; } 
	
	constexpr inline vec3l coordInChunk()  const { return coordinate.mod(fracChunkDim); } 
	vec3i chunk() const { return vec3i(coordinate >> fracChunkDimAsPow2); }
	
	constexpr inline vec3l block()        const { return fracToBlock(coord()    ); }
	constexpr inline vec3i blockInChunk() const { return vec3i(fracToBlock(coordInChunk())); }
	
	constexpr inline vec3l cube()        const { return fracToBlockCube(coord()); }
	constexpr inline vec3i cubeInChunk() const { return vec3i(fracToBlockCube(coordInChunk())); }
	
	constexpr inline vec3d position()        const { return fracToPos(coord()    ); }
	constexpr inline vec3d positionInChunk() const { return fracToPos(coordInChunk()); }
	
	
	friend ChunkCoord operator+(ChunkCoord const c1, ChunkCoord const c2) { return ChunkCoord{c1.coord() + c2.coord()}; }
	friend ChunkCoord &operator+=(ChunkCoord    &c1, ChunkCoord const c2) { return c1 = c1+c2; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, Fractional const offset) { return ChunkCoord{ c1.coord() + offset.it}; } 
	friend ChunkCoord &operator+=(ChunkCoord    &c1, Fractional const offset) { return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, Block const offset) { return c1 + Fractional{blockToFrac(offset.it)}; }
	friend ChunkCoord &operator+=(ChunkCoord    &c1, Block const offset) { return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, Chunk const offset) { return c1 + Fractional{chunkToFrac(offset.it)}; }
	friend ChunkCoord &operator+=(ChunkCoord    &c1, Chunk const offset) { return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, vec3d const offset) { return c1 + Fractional{posToFrac(offset)}; }
	friend ChunkCoord &operator+=(ChunkCoord    &c1, vec3d const offset) { return c1 = c1 + offset; }
	
	
	friend ChunkCoord operator-(ChunkCoord const c1, ChunkCoord const c2) { return ChunkCoord{c1.coord() - c2.coord()}; }
	friend ChunkCoord &operator-=(ChunkCoord    &c1, ChunkCoord const c2) { return c1 = c1-c2; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, Fractional const offset) { return ChunkCoord{ c1.coord() - offset.it}; } 
	friend ChunkCoord &operator-=(ChunkCoord    &c1, Fractional const offset) { return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, Block const offset) { return c1 - Fractional{blockToFrac(offset.it)}; }
	friend ChunkCoord &operator-=(ChunkCoord    &c1, Block const offset) { return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, Chunk const offset) { return c1 - Fractional{chunkToFrac(offset.it)}; }
	friend ChunkCoord &operator-=(ChunkCoord    &c1, Chunk const offset) { return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, vec3d const offset) { return c1 - Fractional{posToFrac(offset)}; }
	friend ChunkCoord &operator-=(ChunkCoord    &c1, vec3d const offset) { return c1 = c1 - offset; }
	
	friend std::ostream& operator<<(std::ostream& stream, ChunkCoord const &v) {
		return stream << "ChunkCoord{" << v.chunk() << ", " << v.positionInChunk() << '}';
	}
};