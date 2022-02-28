#pragma once
#include"Vector.h"
#include<tuple>
#include<iostream>
#include<cmath>

struct ChunkCoord {	
	static constexpr auto fracChunkDim = 1ll << 32;
	static constexpr auto fracBlockDim = 1ll << (32 - Chunks::chunkDimAsPow2);
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
	static constexpr inline Cont<int64_t> blockToFrac(Cont<int32_t> value) { return Cont<int64_t>(value)*fracBlockDim; }
	static constexpr inline vec3l chunkToFrac(vec3i value) { return vec3l(value)*fracChunkDim; }
	
	static constexpr inline vec3i chunkToBlock(vec3i value) { return vec3i(value)*Chunks::chunkDim; }
	
	static constexpr inline vec3d fracToPos(vec3l value) { return static_cast<vec3d>(value) / fracBlockDim; }
	static constexpr inline vec3i fracToBlock(vec3l value) { return value.applied([](auto coord, auto i)->auto{ return static_cast<int32_t>(misc::divFloor<int64_t>(coord, fracBlockDim)); }); }
	static constexpr inline vec3i fracTochunk_(vec3l value) { return value.applied([](auto coord, auto i)->auto{ return static_cast<int32_t>(misc::divFloor<int64_t>(coord, fracChunkDim)); }); }
	
	vec3i chunk_;
	vec3<uint32_t> chunkPart_;
	
	struct Fractional { vec3l it; explicit Fractional(vec3l t) : it{t} {} };
	struct Block      { vec3i it; explicit Block     (vec3i t) : it{t} {} };
		
	ChunkCoord(vec3i chunk__, vec3d positionInChunk) : ChunkCoord(
		chunk__,
		Fractional { posToFrac(positionInChunk) }
	) {}
	
	ChunkCoord(vec3i chunk__, Block block) : ChunkCoord(
		chunk__,
		Fractional{ blockToFrac(block.it) }
	) {}
		
	ChunkCoord(vec3i chunk__, Fractional chunkPart__) {
		chunkPart_ = chunkPart__.it.applied([](auto const coord, auto unused) -> uint32_t { 
			return static_cast<uint32_t>(misc::mod(coord, fracChunkDim));
		});
		chunk_ = chunk__ + fracTochunk_(chunkPart__.it);
	}
	
	ChunkCoord() = default;
	
	ChunkCoord(ChunkCoord const &) = default;
	ChunkCoord(ChunkCoord &&) = default;
	
	ChunkCoord & operator=(ChunkCoord const &) = default;
	ChunkCoord & operator=(ChunkCoord &&) = default;
	
	
	constexpr inline vec3d positionInChunk() const { return fracToPos(chunkPart__long()); }
	constexpr inline vec3i block()    const { return chunkToBlock(chunk_) + fracToBlock(chunkPart__long()); }
	constexpr inline vec3i blockInChunk()    const { return fracToBlock(chunkPart__long()); }
	constexpr inline vec3<uint32_t> chunkPart()  const { return chunkPart_; } 
	constexpr inline vec3l chunkPart__long()  const { return vec3l{ chunkPart_ }; } 
	constexpr inline vec3l position__long()  const { return chunkToFrac(chunk_) + chunkPart__long(); } 
	constexpr inline vec3d position() const { 
		return static_cast<vec3d>(chunk_) * Chunks::chunkDim + positionInChunk(); 
	}
	vec3i chunk() const { return chunk_; }
	
	
	friend ChunkCoord operator+(ChunkCoord const c1, ChunkCoord const c2) 
		{return ChunkCoord(
			c1.chunk_ + c2.chunk_, 
			Fractional{ c1.chunkPart__long() + c2.chunkPart__long() }
		);}
	friend ChunkCoord &operator+=(ChunkCoord &c1, ChunkCoord const c2) { return c1 = c1+c2; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, Fractional const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{c1.chunkPart__long() + offset.it} );}
	friend ChunkCoord &operator+=(ChunkCoord &c1, Fractional const offset) {return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, Block const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{c1.chunkPart__long() + ChunkCoord::blockToFrac(offset.it)} );}
	friend ChunkCoord &operator+=(ChunkCoord &c1, Block const offset) {return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, vec3d const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{ c1.chunkPart__long() + posToFrac(offset) } );}
	friend ChunkCoord &operator+=(ChunkCoord &c1, vec3d const offset) {return c1 = c1 + offset; }
	
	
	friend ChunkCoord operator-(ChunkCoord const c1, ChunkCoord const c2) 
		{return ChunkCoord(
			c1.chunk_ - c2.chunk_, 
			Fractional{ c1.chunkPart__long() - c2.chunkPart__long() }
		);}
	friend ChunkCoord &operator-=(ChunkCoord &c1, ChunkCoord const c2) { return c1 = c1-c2; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, Fractional const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{c1.chunkPart__long() - offset.it} );}
	friend ChunkCoord &operator-=(ChunkCoord &c1, Fractional const offset) {return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, Block const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{c1.chunkPart__long() - ChunkCoord::blockToFrac(offset.it)} );}
	friend ChunkCoord &operator-=(ChunkCoord &c1, Block const offset) {return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, vec3d const offset) 
		{return ChunkCoord( c1.chunk_, Fractional{ c1.chunkPart__long() - posToFrac(offset) } );}
	friend ChunkCoord &operator-=(ChunkCoord &c1, vec3d const offset) {return c1 = c1 - offset; }
	
	
	friend std::ostream& operator<<(std::ostream& stream, ChunkCoord const &v) {
		return stream << '(' << v.chunk_ << ',' << v.positionInChunk() << ')';
	}
};