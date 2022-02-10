#pragma once
#include"Vector.h"
#include<tuple>
#include<iostream>

class ChunkCoord {
private:
	vec3i chunk_;
	vec3d inChunkPosition_;
public:
	ChunkCoord() = default;
	ChunkCoord(vec3i chunk__, vec3d inChunkPosition__) {
		std::tie(chunk_, inChunkPosition_) = Chunks::normalizePos(chunk__, inChunkPosition__);
	}
	ChunkCoord(vec3d const position) {
		vec3d const newInChunkCoord{ Chunks::inChunkCoord(position) };
		vec3i const newChunk{ Chunks::asChunkCoord(position) };
		inChunkPosition_ = newInChunkCoord;
		chunk_ = newChunk;
	}
	
	ChunkCoord(ChunkCoord const &) = default;
	ChunkCoord(ChunkCoord &&) = default;
	
	ChunkCoord & operator=(ChunkCoord const &) = default;
	ChunkCoord & operator=(ChunkCoord &&) = default;
private:
	ChunkCoord(vec3i chunk__, vec3d inChunkPosition__, bool unused) : chunk_{chunk__}, inChunkPosition_{inChunkPosition__} {}
public:
	inline vec3i const &chunk() const { return chunk_; }	
	inline vec3d const &inChunkPosition() const { return inChunkPosition_; }	
	inline vec3d position() const { return static_cast<vec3d>(chunk_)*Chunks::chunkDim + inChunkPosition_; }	
	
	friend ChunkCoord operator+(ChunkCoord const c1, ChunkCoord const c2) {
		return ChunkCoord(
			c1.chunk_+c2.chunk_, 
			c1.inChunkPosition_+c2.inChunkPosition_
		);
	}
	
	friend ChunkCoord &operator+=(ChunkCoord &c1, ChunkCoord const c2) {
		return c1 = c1+c2;
	}
	
	friend ChunkCoord operator+(ChunkCoord const c1, vec3d const offset) {
		auto a = ChunkCoord(c1);
		return a += offset;
	}
	
	friend ChunkCoord &operator+=(ChunkCoord &c1, vec3d const offset) {
		c1.inChunkPosition_ += offset;
		auto const chunkOffset{ Chunks::asChunkCoord(c1.inChunkPosition_) };
		c1.inChunkPosition_ = Chunks::inChunkCoord(c1.inChunkPosition_);
		c1.chunk_ += chunkOffset;
		return c1;
	}
	
	
	friend ChunkCoord operator-(ChunkCoord const c1, ChunkCoord const c2) {
		return ChunkCoord(
			c1.chunk_-c2.chunk_, 
			c1.inChunkPosition_-c2.inChunkPosition_
		);
	}
	
	friend ChunkCoord &operator-=(ChunkCoord &c1, ChunkCoord const c2) {
		return c1 = c1-c2;
	}
	
	friend std::ostream& operator<<(std::ostream& stream, ChunkCoord const &v) {
		return stream << '(' << v.chunk_ << ',' << v.inChunkPosition_ << ')';
	}
};