#pragma once
#include"Vector.h"
#include<tuple>
#include<iostream>
#include<cmath>

struct ChunkCoord {
private:
static constexpr auto fracChunkDim = 1ll << 32;
static constexpr auto fracBlockDim = 1ll << (32 - Chunks::chunkDimAsPow2);

struct fractional { vec3l it; explicit fractional(vec3l t) : it{t} {} };

	vec3i chunk_;
	vec3<uint32_t> chunkPart;
public:
	
	ChunkCoord() = default;
	
	ChunkCoord(vec3i chunk__, vec3d positionInChunk) : ChunkCoord(
		chunk__ + vec3i((positionInChunk / Chunks::chunkDim).appliedFunc<double(*)(double)>(floor)),
		fractional { positionInChunk.applied([](auto const coord, auto ignored) -> int64_t { 
				return int64_t(misc::modd(coord, Chunks::chunkDim) * fracBlockDim); 
		})}
	) {}
	
		//ChunkCoord(vec3d const position) {
	//	vec3d const newInChunkCoord{ Chunks::inChunkCoord(position) };
	//	vec3i const newChunk{ Chunks::asChunkCoord(position) };
	//	chunkPart_ = newInChunkCoord;
	//	chunk__ = newChunk;
	//}
	
	ChunkCoord(ChunkCoord const &) = default;
	ChunkCoord(ChunkCoord &&) = default;
	
	ChunkCoord & operator=(ChunkCoord const &) = default;
	ChunkCoord & operator=(ChunkCoord &&) = default;
	
private:
	ChunkCoord(vec3i chunk__, fractional chunkPart_) {
		chunkPart = chunkPart_.it.applied([](auto const coord, auto unused) -> uint32_t { 
			return static_cast<uint32_t>(misc::mod(coord, fracChunkDim));
		});
		chunk_ = chunk__ + chunkPart_.it.applied([&](auto const coord, auto i) -> int32_t { 
			return static_cast<int32_t>( coord / fracChunkDim - (coord < 0 && chunkPart[i] != 0) );
		});
	}
	constexpr inline vec3l inChunk_long() const { return vec3l{ chunkPart }; } 
public:
	
	constexpr inline vec3d positionInChunk() const { return static_cast<vec3d>(chunkPart) / fracBlockDim; }
	constexpr inline vec3i blockInChunk()    const { return chunkPart / fracBlockDim; }
	constexpr inline vec3d position() const { 
		return static_cast<vec3d>(chunk_) * (Chunks::chunkDim) + positionInChunk(); 
	}	
	constexpr inline vec3i chunk() const { return chunk_; }
	
	
	friend ChunkCoord operator+(ChunkCoord const c1, ChunkCoord const c2) {
		return ChunkCoord(
			c1.chunk_ + c2.chunk_, 
			fractional{c1.inChunk_long() + c2.inChunk_long()}
		);
	}
	friend ChunkCoord &operator+=(ChunkCoord &c1, ChunkCoord const c2) { return c1 = c1+c2; }
	
	//friend ChunkCoord operator+(ChunkCoord const c1, vec3l const offset) {
	//	return ChunkCoord( c1.chunk_, fractional{c1.inChunk_long() + offset} );
	//}
	//friend ChunkCoord &operator+=(ChunkCoord &c1, vec3l const offset) {return c1 = c1 + offset; }
	
	friend ChunkCoord operator+(ChunkCoord const c1, vec3d const offset) {
		return ChunkCoord( c1.chunk_, fractional{c1.inChunk_long() + vec3l(offset * fracBlockDim)} );
	}
	friend ChunkCoord &operator+=(ChunkCoord &c1, vec3d const offset) {return c1 = c1 + offset; }
	
	
	friend ChunkCoord operator-(ChunkCoord const c1, ChunkCoord const c2) {
		return ChunkCoord( 
			c1.chunk_ - c2.chunk_, 
			fractional{c1.inChunk_long() - c2.inChunk_long()}
		);
	}
	friend ChunkCoord &operator-=(ChunkCoord &c1, ChunkCoord const c2) { return c1 = c1-c2; }
	
	//friend ChunkCoord operator-(ChunkCoord const c1, vec3l const offset) {
	//	return ChunkCoord( c1.chunk_, fractional{c1.inChunk_long() - offset} );
	//}
	//friend ChunkCoord &operator-=(ChunkCoord &c1, vec3l const offset) {return c1 = c1 - offset; }
	
	friend ChunkCoord operator-(ChunkCoord const c1, vec3d const offset) {
		return ChunkCoord( c1.chunk_, fractional{c1.inChunk_long() - vec3l(offset * fracBlockDim)} );
	}
	friend ChunkCoord &operator-=(ChunkCoord &c1, vec3d const offset) {return c1 = c1 - offset; }
	
	
	friend std::ostream& operator<<(std::ostream& stream, ChunkCoord const &v) {
		return stream << '(' << v.chunk_ << ',' << v.chunkPart << ')';
	}
};