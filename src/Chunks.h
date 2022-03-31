#pragma once

#include<vector>
#include<unordered_map>
#include<stdint.h>
#include<array>
#include<tuple>
#include<utility>

#include"Vector.h"



struct Chunks {
public:
	static constexpr int const chunkDimAsPow2 = 4;
	static constexpr int const chunkDim = 1 << chunkDimAsPow2; //used in vertex.shader
	static constexpr int const chunkSize = chunkDim*chunkDim*chunkDim;
	//static constexpr int const tmpChunkSize = chunkDim*chunkDim*chunkDim/(sizeof(uint16_t) * 8);
	
	struct Block { //used in main.shader
	private:
		uint32_t data_;
	public:
		static constexpr uint8_t cubePosIndex(vec3b const pos) {
			return pos.x + pos.y * 2 + pos.z * 4;
		}
		static constexpr vec3b cubeIndexPos(uint8_t const index) {
			return vec3b( index & 1, (index / 2) & 1, (index / 4) & 1 );
		}
		static constexpr uint8_t blockCubeMask(uint8_t const index) {
			return 1 << index;
		}
		static constexpr uint8_t blockCubeMask(vec3b const upperHalf) {
			const auto index{ cubePosIndex(upperHalf) };
			return 1 << index;
		}
		static constexpr bool blockCube(uint8_t const cubes, uint8_t const index) {
			return (cubes >> index) & 1;
		}
		static constexpr bool blockCube(uint8_t const cubes, vec3b const upperHalf) {
			const auto index{ cubePosIndex(upperHalf) };
			return (cubes >> index) & 1;
		}
		static constexpr Block fullBlock(uint16_t const id) { return Block(id, 0b1111'1111); }
		static constexpr Block emptyBlock() { return Block(0, 0); }
		
		Block() = default;
		explicit constexpr Block(uint32_t const data__) : data_{ data__ } {}
		constexpr Block(uint16_t const id, uint8_t cubes) : data_{ uint32_t(id) | (uint32_t(cubes) << 24) } {
			if(id == 0 || cubes == 0) data_ = 0;
		}
		
		constexpr uint16_t id() const { return uint16_t(data_ & ((1 << 16) - 1)); }
		constexpr bool cube(vec3b const upperHalf) const { return blockCube(cubes(), upperHalf); }
		constexpr bool cube(uint8_t const index) const { return blockCube(cubes(), index); }
		constexpr bool empty() const { return data_ == 0; }
		
		uint8_t cubes() const { return uint8_t(data_ >> 24); }
		uint32_t data() const { return data_; }
	};
	
	using ChunkData = std::array<Block, chunkSize>;
	//using tmpChunkData = std::array<uint8_t, tmpChunkSize>;
	
	struct AABB {
		static constexpr int64_t cd = chunkDim-1;
		static_assert( cd*cd*cd*cd*cd*cd < (1ll << 32), "two block indices must fit into 32 bits" );
		uint32_t data;
		
		AABB() = default;
		//AABB(int16_t const b1, int16_t const b2) : data{ uint32_t(uint16_t(b1)) | (uint32_t(uint16_t(b2)) << 16) } {}
		AABB(vec3i const start, vec3i const end) : data{ 
			uint32_t(uint16_t(blockIndex(start)))
			| (uint32_t(uint16_t(blockIndex(end))) << 16) 
		} {}
		
		constexpr uint32_t getData() const { return data; } //used in vectex.shader, debug program
		constexpr vec3i start() const { return indexBlock(int16_t(data&0xffff)); } //used in vectex.shader, debug program
		constexpr vec3i end() const { return indexBlock(int16_t(data>>16)); } //used in main vertex shader //used in vectex.shader, debug program
		constexpr vec3i onePastEnd() const { return end() + 1; } //used in main vertex shader //used in vectex.shader, debug program
		constexpr bool empty() const { return (end() < start()).any(); };
	};
	
	struct OptionalChunkIndex {
	// -(chunkIndex) - 1	
	private: int n;
	public:
		OptionalChunkIndex() = default;
		OptionalChunkIndex(int chunkIndex) : n{ -chunkIndex - 1 } {}
		explicit operator int() const { return get(); }
		
		operator bool() const { return is(); }
		bool is() const {
			return n != 0;
		}
		
		int32_t get() const { //return -1 if invalid
			return int32_t(int64_t(n + 1) * -1); //-n - 1 is UB if n is integer min?
		}
	};
	
	using OptionalNeighbour = OptionalChunkIndex;
	
	struct Neighbours {
		static constexpr int neighboursCount{ 6 };
		std::array<OptionalNeighbour, neighboursCount> n;
		
		static constexpr bool checkIndexValid(uint8_t const index) {
			return index < neighboursCount;
		}
		
		static constexpr bool checkDirValid(vec3i const dir) {
			return vec3i(dir.notEqual(0)).dot(1) == 1;
		}
		
		//used in main.shader
			static constexpr vec3i indexAsDir(uint8_t neighbourIndex) {
				assert(checkIndexValid(neighbourIndex));
				vec3i const dirs[] = { vec3i{-1,0,0},vec3i{1,0,0},vec3i{0,-1,0},vec3i{0,1,0},vec3i{0,0,-1},vec3i{0,0,1} };
				return dirs[neighbourIndex];
				//return vec3i{
				//	(neighbourIndex / 1) % 3,
				//	(neighbourIndex / 3) % 3,
				//	(neighbourIndex / 9) % 3
				//} - 1;
			}
			static constexpr uint8_t dirAsIndex(vec3i dir) {
				assert(checkDirValid(dir));
				auto const result{ (dir.x+1)/2 + (dir.y+1)/2+abs(dir.y*2) + (dir.z+1)/2+abs(dir.z*4) };
				if(indexAsDir(result) != dir) {
					std::cerr << "err: " << dir << ' ' << result << '\n';
					assert(false);
				}
				return result; 
				//return uint8_t( dir.x+1 + (dir.y+1)*3 + (dir.z+1)*9 );
			}
			static constexpr uint8_t mirror(uint8_t index) {
				return dirAsIndex( -indexAsDir(index) );
			}
			
			static constexpr bool isSelf(uint8_t index) {
				return false;
				//return indexAsDir(index) == 0;
			}
		
		OptionalNeighbour &operator[](uint8_t index) { assert(checkIndexValid(index)); return n[index]; }
		OptionalNeighbour const &operator[](uint8_t index) const { assert(checkIndexValid(index)); return n[index]; }
		
		OptionalNeighbour &operator[](vec3i dir) { return n[dirAsIndex(dir)]; }
		OptionalNeighbour const &operator[](vec3i dir) const { return n[dirAsIndex(dir)]; }
		
		//OptionalNeighbour *begin() { return n.begin(); }
		//OptionalNeighbour *end  () { return n.end  (); }
		
		//OptionalNeighbour const *cbegin() const { return n.cbegin(); }
		//OptionalNeighbour const *cend  () const { return n.cend  (); }
	}; 
	
	struct Chunk {
	private:
		Chunks *chunks_;
		int chunk_index;
	public:
		Chunk() = default;
		
		Chunk(Chunks &chunks, int const chunkIndex) : chunks_{ &chunks }, chunk_index{ chunkIndex } {}
			
		auto &chunks() { return *chunks_; }	
		auto const &chunks() const { return *chunks_; }

		auto &chunkIndex() { return chunk_index; }	
		auto const &chunkIndex() const { return chunk_index; }	
			
		#define gs(name, accessor) decltype(auto) name () { return chunks(). accessor [chunk_index]; } decltype(auto) name () const { return chunks(). accessor [chunk_index]; }	
			gs(position, chunksPos)
			gs(aabb, chunksAABB)
			gs(gpuPresent, gpuPresent) //is this safe? (gpuPresent returns rvalue reference)
			gs(modified, modified) //is this safe? (gpuPresent returns rvalue reference)
			gs(data, chunksData)
			gs(neighbours, chunksNeighbours)
		#undef gs
	};

	struct Move_to_neighbour_Chunk {
	private:
	Chunks::Chunk chunk;
	bool valid;
	public:
		Move_to_neighbour_Chunk() = default;
		Move_to_neighbour_Chunk(Chunks::Chunk &src) : chunk{src}, valid{ true } {}
			
		Move_to_neighbour_Chunk(Chunks &chunks, vec3i const chunkCoord) {
			//int32_t chunkIndex = -1;
				
			//for(auto const elChunkIndex : chunks.used)
			//	if(chunks.chunksPos[elChunkIndex] == chunkCoord) { chunkIndex = elChunkIndex; break; }
		
			auto const chunkIndexP{ chunks.chunksIndex_position.find(chunkCoord) };
			
			valid = chunkIndexP != chunks.chunksIndex_position.end();
			
			if(valid) chunk = chunks[chunkIndexP->second];
		}
		
		OptionalChunkIndex optChunk() const {
			if(valid) return OptionalChunkIndex{ chunk.chunkIndex() };
			return {};
		}
		
		OptionalChunkIndex move(vec3i const otherChunk, int index) {
			if(valid && Neighbours::checkDirValid(otherChunk - chunk.position())) return offset(otherChunk - chunk.position(), index);
			*this = Move_to_neighbour_Chunk(chunk.chunks(), otherChunk);
			return optChunk();
		}
		
		OptionalChunkIndex moveToNeighbour(vec3i const neighbour, int index) {
			if(!valid) return {};
			return offset(neighbour - chunk.position(), index);
		}
			
		OptionalChunkIndex offset(vec3i const dir, int index) {
			if(!Neighbours::checkDirValid(dir)) {
				std::cerr << "dir " << index << " is invalid:" << dir << '\n';
				assert(false);
				exit(-1);
			}
			if(!valid) return {};
			if(dir == 0) return { chunk.chunkIndex() };
			auto const optChunkIndex{ chunk.neighbours()[dir] };
			valid = optChunkIndex.is();
			if(valid) chunk = chunk.chunks()[optChunkIndex.get()];
			return optChunkIndex;
		}
		
		bool is() const { return valid; }
		Chunks::Chunk get() const { return chunk; };
	};
private:
	std::vector<int> vacant{};
	std::vector<int> used_{};
	
	struct PosHash { 
		constexpr inline std::size_t operator()(vec3i const &it) const noexcept { 
			return  (std::hash<int32_t>{}(it.x) ^ (std::hash<int32_t>{}(it.x) << 1)) ^ (std::hash<int32_t>{}(it.z) << 1);
		} 
	};
public:
	std::vector<vec3i> chunksPos{};
	std::vector<int> used{};
	std::vector<AABB> chunksAABB{};
	std::vector<bool> gpuPresent{};
	std::vector<bool> modified{};
	std::vector<ChunkData> chunksData{};
	std::vector<Neighbours> chunksNeighbours{};
	std::unordered_map<vec3i, int, PosHash> chunksIndex_position{};
	
	//returns used[] position 
	//all the chunk data is not initialised
	inline int reserve() {
		int index;
		int usedSize = used.size();

		if(!vacant.empty()) { 
			index = vacant[vacant.size()-1];
			vacant.pop_back();
		}
		else { //TODO: avoid zero-init
			index = usedSize;
			chunksPos.resize(index+1);
			chunksAABB.resize(index+1);
			gpuPresent.resize(index+1);
			modified.resize(index+1);
			chunksData.resize(index+1);
			chunksNeighbours.resize(index+1);
		}
		used.push_back(index);
		
		return usedSize;
	}

	inline void recycle(int const index) {
		auto chunkIndex = used[index];
		chunksNeighbours[chunkIndex] = Neighbours();
		used.erase(used.begin()+index);
		vacant.push_back(chunkIndex);
	}

	
	template<typename Action>
	inline void forEachUsed(Action &&action) const {
		for(auto const chunkIndex : used) {
			action(chunkIndex);
		}
	}
	
	template<typename Predicate, typename Free>
	inline void filterUsed(Predicate&& keep, Free &&free) {
		auto const sz = used.size();
		
		for(size_t i = 0; i < sz; ++i) {
			auto const &chunkIndex = used[i];
			if(keep(chunkIndex)) used_.push_back(chunkIndex);
			else { free(chunkIndex); vacant.push_back(chunkIndex); }
		}
		
		used.clear();
		used.swap(used_);
	}
	
	inline Chunk operator[](int chunkIndex) {
		return Chunk{ *this, chunkIndex };
	}
	
	inline static constexpr int16_t blockIndex(vec3<int32_t> position) {
		return position.x + position.y*Chunks::chunkDim + position.z*Chunks::chunkDim*Chunks::chunkDim;
	}
	
	inline static constexpr vec3i indexBlock(int16_t index) { //used in vectex.shader
		return vec3i{ index % chunkDim, (index / chunkDim) % chunkDim, (index / chunkDim / chunkDim) };
	}
	
	inline static constexpr bool checkIsInChunk(vec3i const blockCoord) {
		return blockCoord.inMMX(vec3i{0}, vec3i{chunkDim}).all();
	}
};