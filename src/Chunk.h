#pragma once

#include"Units.h"
#include"Position.h"
#include"Vector.h"
#include"Area.h"

#include<vector>
#include<unordered_map>
#include<stdint.h>
#include<array>
#include<tuple>
#include<utility>
#include<algorithm>
#include<type_traits>

static constexpr int chunkColumnChunkYMax = 15;
static constexpr int chunkColumnChunkYMin = -16;

static constexpr int chunksCoumnChunksCount{ chunkColumnChunkYMax - chunkColumnChunkYMin + 1 };

namespace chunk {	
	using BlockInChunkIndex = uint16_t; static_assert( pos::cubed((uChunk{1}.as<uBlock>() - 1).val()) < (1 << 15) );
	using CubeInChunkIndex  = uint16_t; static_assert( pos::cubed((uChunk{1}.as<uCube >() - 1).val()) < (1 << 15) );
	
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wtautological-constant-out-of-range-compare"
	inline constexpr bool checkBlockCoordInChunkValid(vec3i const coord) {
		return coord.inMMX(vec3i{0}, vec3i{units::blocksInChunkDim}).all();
	}
	inline constexpr bool checkBlockCoordInChunkValid(pBlock const coord) {
		return checkBlockCoordInChunkValid(coord.val());
	}
	inline constexpr bool checkBlockIndexInChunkValid(uint16_t const index) {
		return index >= 0 && index < pos::blocksInChunkCount;
	}
	
	inline constexpr bool checkCubeCoordInChunkValid(vec3i const coord) {
		return coord.inMMX(vec3i{0}, vec3i{units::cubesInChunkDim}).all();
	}
	inline constexpr bool checkCubeCoordInChunkValid(pCube const coord) {
		return checkCubeCoordInChunkValid(coord.val());
	}
	inline constexpr bool checkCubeIndexInChunkValid(uint16_t const index) {
		return index >= 0 && index < pos::cubesInChunkCount;
	}
	
	inline constexpr bool checkCubeCoordInBlockValid(pCube const coord) {
		return coord.val().inMMX(vec3i{0}, vec3i{units::cubesInBlockDim}).all();
	}
	
	inline constexpr bool checkCubeIndexInBlockValid(uint8_t const index) {
		return index < pos::cubesInBlockCount;
	}
  #pragma clang diagnostic pop
	  
	//used in main.shader
	  inline constexpr int16_t blockIndex(vec3i const coord) {
		  assert(checkBlockCoordInChunkValid(coord));
		  return coord.x + coord.y*units::blocksInChunkDim + coord.z*units::blocksInChunkDim*units::blocksInChunkDim;
	  }
	  inline constexpr vec3i indexBlock(uint16_t index) {
		  assert(checkBlockIndexInChunkValid(index));
		  return vec3i{ 
		 	 index % units::blocksInChunkDim, 
		 	 (index / units::blocksInChunkDim) % units::blocksInChunkDim, 
		 	 (index / units::blocksInChunkDim / units::blocksInChunkDim) 
		  };
	  }
	  
	  inline constexpr vec3i cubeCoordInChunk(uint16_t const index) {
		  assert(checkCubeIndexInChunkValid(index));
		  return vec3i{
		  	  index % units::cubesInChunkDim, 
		  	  (index / units::cubesInChunkDim) % units::cubesInChunkDim, 
		  	  (index / units::cubesInChunkDim / units::cubesInChunkDim) 
		  };
	  }
	  inline constexpr uint32_t cubeIndexInChunk(vec3i const coord) {
		  assert(checkCubeCoordInChunkValid(coord));
		  return coord.x + coord.y*units::cubesInChunkDim + coord.z*units::cubesInChunkDim*units::cubesInChunkDim;
	  }
	  
	  inline constexpr BlockInChunkIndex blockCoordToIndex(pBlock const coord) { return blockIndex(coord.val()); }
	  inline constexpr pBlock blockIndexToCoord(BlockInChunkIndex const index) { return pBlock{indexBlock(index)}; }
	  
	  inline constexpr CubeInChunkIndex cubeCoordToIndex(pCube const coord) { return cubeIndexInChunk(coord.val()); }
	  inline constexpr pCube cubeIndexToCoord(CubeInChunkIndex const index) { return pCube{cubeCoordInChunk(index)}; }
	  
	  inline constexpr uint8_t cubeCoordInBlockToIndex(pCube const pos) {
		assert(checkCubeCoordInBlockValid(pos));
		auto const coord{ pos.val() };
		return coord.x + (coord.y << units::cubesInBlockDimAsPow2) + (coord.z << (units::cubesInBlockDimAsPow2*2));
	}
	
	inline constexpr pCube cubeIndexInBlockToCoord(uint8_t const index) {
		assert(checkCubeIndexInBlockValid(index));
		return pCube(
			 index                                      % units::cubesInBlockDim, 
			(index >>  units::cubesInBlockDimAsPow2   ) % units::cubesInBlockDim, 
			(index >> (units::cubesInBlockDimAsPow2*2)) % units::cubesInBlockDim
		);
	}
	
	template<typename Unit> struct PackedAABB;
	
	template<> struct PackedAABB<pBlock> { //used in main.frag
		static constexpr int64_t cd = units::blocksInChunkDim-1;
		static_assert( cd*cd*cd * cd*cd*cd < (1ll << 32), "two block indices must fit into 32 bits" );
	private:
		uint32_t data;
	public:
		PackedAABB() = default;
		PackedAABB(pBlock const start, pBlock const end) : data{ 
			uint32_t(chunk::blockCoordToIndex(start))
			| (uint32_t(chunk::blockCoordToIndex(end)) << 16) 
		} {}
		PackedAABB(Area const a) : PackedAABB{ a.first, a.last } {}
		
		constexpr pBlock first() const { return chunk::blockIndexToCoord(int16_t(data&0xffff)); }
		constexpr pBlock last() const { return chunk::blockIndexToCoord(int16_t(data>>16)); } 
	};
	
	template<> struct PackedAABB<pCube> { //used in main.frag
		static constexpr int64_t cd = units::cubesInChunkDim-1;
		static_assert( cd*cd*cd * cd*cd*cd < (1ll << 32), "two cube indices must fit into 32 bits" );
	private:
		uint32_t data;
	public:
		PackedAABB() = default;
		PackedAABB(pCube const start, pCube const end) : data{ 
			uint32_t(chunk::cubeCoordToIndex(start))
			| (uint32_t(chunk::cubeCoordToIndex(end)) << 16) 
		} {}
		PackedAABB(Area const a) : PackedAABB{ a.first, a.last } {}
		
		constexpr pCube first() const { return chunk::cubeIndexToCoord(int16_t(data&0xffff)); }
		constexpr pCube last() const { return chunk::cubeIndexToCoord(int16_t(data>>16)); } 
	};
	
	template<typename Chunks>
	struct Chunk_{
	private:
		Chunks *chunks_;
		int chunk_index;
	public:
		Chunk_() = default;
		
		Chunk_(Chunks &chunks, int const chunkIndex) : chunks_{ &chunks }, chunk_index{ chunkIndex } {}	
		
		bool operator==(Chunk_<Chunks> const other) const {
			return chunks_ == other.chunks_ && chunk_index == other.chunk_index;
		}
		
		bool operator!=(Chunk_<Chunks> const other) const {
			return !(*this == other);
		}	
		
		auto &chunks() const { return *chunks_; }
		
		auto &chunkIndex() { return chunk_index; }	
		auto const &chunkIndex() const { return chunk_index; }	
			
		#define gs(name, accessor) decltype(auto) name () { return chunks(). accessor [chunk_index]; } decltype(auto) name () const { return chunks(). accessor [chunk_index]; }	
			gs(position, chunksPos)
			gs(aabb, chunksAABB)
			gs(status, chunksStatus)
			gs(modified, modified)
			gs(blocksData, blocksData)
			gs(data, chunksData)
			gs(blocks, chunksData) //same as data()
			gs(liquid, chunksLiquid)
			gs(neighbours, chunksNeighbours)
			gs(ao, chunksAO)
			gs(skyLighting, chunksSkyLighting)
			gs(blockLighting, chunksBlockLighting)
			gs(emitters, chunksEmitters)
			gs(neighbouringEmitters, chunksNeighbouringEmitters)
			gs(gpuIndex, chunksGPUIndex)
		#undef gs
	};
	
	struct Chunks;
	using Chunk = Chunk_<Chunks>;

	struct Block { //used in main.shader
		static_assert(pos::cubesInBlockCount <= 8, "cubes state must fit into 8 bits");
		
		using id_t = uint16_t;
		using cubes_t = uint8_t;
		
		static constexpr bool checkCubeCoordValid(vec3i const coord) {
			return checkCubeCoordInBlockValid(pCube{coord});
		}
		
		static constexpr bool checkCubeIndexValid(uint8_t const index) {
			return checkCubeIndexInBlockValid(index);
		}
		
		static constexpr uint8_t cubePosIndex(vec3i const pos) {
			return cubeCoordInBlockToIndex(pCube{pos});
		}
		
		static constexpr vec3i cubeIndexPos(uint8_t const index) {
			return cubeIndexInBlockToCoord(index).val();
		}
		
		static constexpr uint8_t blockCubeMask(uint8_t const index) {
			assert(checkCubeIndexValid(index));
			return 1 << index;
		}
		
		static constexpr uint8_t blockCubeMask(vec3i const coord) {
			const auto index{ cubePosIndex(coord) };
			return 1 << index;
		}
		
		static constexpr bool blockCube(uint8_t const cubes, uint8_t const index) {
			assert(checkCubeIndexValid(index));
			return (cubes >> index) & 1;
		}
		
		static constexpr bool blockCube(uint8_t const cubes, vec3i const coord) {
			const auto index{ cubePosIndex(coord) };
			return (cubes >> index) & 1;
		}
		
		static constexpr Block fullBlock(id_t const id) { return Block(id, 0b1111'1111); }
		static constexpr Block emptyBlock() { return Block(0, 0); }
		
		static constexpr Block idChanged(Block const it, id_t const id) { return Block{id, it.cubes()}; }
		static constexpr Block cubesChanged(Block const it, uint8_t const cubes) { return Block{it.id(), cubes}; }
	private:
		uint32_t data_;
	public:
		Block() = default;
		explicit constexpr Block(uint32_t const data__) : data_{ data__ } {}
		constexpr Block(id_t const id, cubes_t const cubes) : data_{ uint32_t(id) | (uint32_t(cubes) << 24) } {
			if(id == 0 || cubes == 0) data_ = 0;
		}
		uint32_t data() const { return data_; }
		cubes_t cubes() const { return cubes_t(data_ >> 24); }
		constexpr id_t id() const { return id_t(data_ & ((1 << 16) - 1)); }
		
		constexpr bool cube(vec3i const coord) const { return blockCube(cubes(), coord); }
		constexpr bool cube(uint8_t const index) const { return blockCube(cubes(), index); }
		
		constexpr bool cubeAtCoord(vec3i const coord) const { return blockCube(cubes(), coord); }
		constexpr bool cubeAtIndex(uint8_t const index) const { return blockCube(cubes(), index); }
		
		explicit operator bool() const { return id() != 0; }
		constexpr bool isEmpty() const { return id() == 0; }
		constexpr bool empty() const { return isEmpty(); }	
	};
	
	struct OptionalChunkIndex { //used in main.shader
	// -(chunkIndex) - 1	
	private: int n;
	public:
		OptionalChunkIndex() = default;
		OptionalChunkIndex(int chunkIndex) : n{ -chunkIndex - 1 } {}
		explicit operator int() const { return get(); }
		
		//operator bool() const { return is(); }
		bool is() const {
			return n != 0;
		}
		
		int32_t get() const { //return -1 if invalid
			return int32_t(int64_t(n + 1) * -1); //-n - 1 is UB if n is integer min?
		}
		
		bool operator==(OptionalChunkIndex const it) const {
			return it.n == n;
		}		
		
		bool operator!=(OptionalChunkIndex const it) const {
			return it.n != n;
		}
	};
	
	
	struct Neighbours {
		static constexpr int neighboursCount{ 6 };
		
		static constexpr bool checkIndexValid(uint8_t const index) {
			return index < neighboursCount;
		}
		
		static constexpr bool checkDirValid(vec3i const dir) {
			return vec3i(dir.notEqual(0)).dot(1) == 1 && dir.abs().equal(1).any();
		}
		
		//used in main.shader
			static constexpr vec3i indexAsDir(uint8_t neighbourIndex) {
				assert(checkIndexValid(neighbourIndex));
				vec3i const dirs[] = { vec3i{-1,0,0},vec3i{1,0,0},vec3i{0,-1,0},vec3i{0,1,0},vec3i{0,0,-1},vec3i{0,0,1} };
				return dirs[neighbourIndex];
			}
			
			static uint8_t dirAsIndex(vec3i const dir) {
				assert(checkDirValid(dir));
				auto const result{ (dir.x+1)/2 + (dir.y+1)/2+abs(dir.y*2) + (dir.z+1)/2+abs(dir.z*4) };
				if(indexAsDir(result) != dir) {
					std::cerr << "err: " << dir << ' ' << result << '\n';
					assert(false);
				}
				return result; 
			}
			static uint8_t mirror(uint8_t index) {
				return dirAsIndex(-indexAsDir(index));
			}
			static uint8_t mirror(vec3i const dir) {
				return dirAsIndex(-dir);
			}
		
		std::array<OptionalChunkIndex, neighboursCount> n;
		
		OptionalChunkIndex &operator[](uint8_t index) { assert(checkIndexValid(index)); return n[index]; }
		OptionalChunkIndex const &operator[](uint8_t index) const { assert(checkIndexValid(index)); return n[index]; }
		
		OptionalChunkIndex &operator[](vec3i dir) { return n[dirAsIndex(dir)]; }
		OptionalChunkIndex const &operator[](vec3i dir) const { return n[dirAsIndex(dir)]; }
	};
	
	
	struct ChunkStatus {
	private:
		uint8_t updateAO : 1;
		uint8_t updateLightingAdd : 1;
		uint8_t updateNeighbouringEmitters : 1;
		
		uint8_t aoUpdated : 1;
		uint8_t blocksUpdated : 1;
		uint8_t lightingUpdated : 1;
		uint8_t neighbouringEmittersUpdated : 1;
	public:
		ChunkStatus() = default;
		
		void setEverythingUpdated() {
			setAOUpdated(true);
			setBlocksUpdated(true);
			setLightingUpdated(true);
			setNeighbouringEmittersUpdated(true);
		}
		
		bool isInvalidated() const { return aoUpdated || blocksUpdated || lightingUpdated || neighbouringEmittersUpdated; }
		bool needsUpdate()   const { return updateAO || updateLightingAdd || updateNeighbouringEmitters; }	
		
		bool isAOUpdated() const { return aoUpdated; }
		void setAOUpdated(bool const val) { aoUpdated = val; }		
		
		bool isBlocksUpdated() const { return blocksUpdated; }
		void setBlocksUpdated(bool const val) { blocksUpdated = val; } 
		
		bool isLightingUpdated() const { return lightingUpdated; }
		void setLightingUpdated(bool const val) { lightingUpdated = val; }		

		bool isNeighbouringEmittersUpdated() const { return neighbouringEmittersUpdated; }
		void setNeighbouringEmittersUpdated(bool const val) { neighbouringEmittersUpdated = val; }
		

		bool isUpdateAO() const { return updateAO; }
		void setUpdateAO(bool const val) { updateAO = val; }		
		
		bool isUpdateLightingAdd() const { return updateLightingAdd; }
		void setUpdateLightingAdd(bool const val) { updateLightingAdd = val; }
		
		bool isUpdateNeighbouringEmitters() const { return updateNeighbouringEmitters; }
		void setUpdateNeighbouringEmitters(bool const it) { updateNeighbouringEmitters = it; }
		
	};
	
	
	template<typename T>
	struct CubesArray {
		static constexpr int size = pos::cubesInChunkCount;
		using value_type = T;
	private:
		std::array<value_type, size> data;
	public:		
		CubesArray() = default;
		explicit CubesArray(T const value) { fill(value); }
	
		value_type       &operator[](int const index)       { assert(checkCubeIndexInChunkValid(index)); return data[index]; }
		value_type const &operator[](int const index) const { assert(checkCubeIndexInChunkValid(index)); return data[index]; }
		
		value_type       &operator[](pCube const coord)       { assert(checkCubeCoordInChunkValid(coord)); return (*this)[cubeCoordToIndex(coord)]; }
		value_type const &operator[](pCube const coord) const { assert(checkCubeCoordInChunkValid(coord)); return (*this)[cubeCoordToIndex(coord)]; }
		
		void fill(T const value) { data.fill(value); }
		void reset() { data.fill(T()); }
	};
	
	template<typename T>
	struct BlocksArray {
		static constexpr int size = pos::blocksInChunkCount;
		using value_type = T;
	private:
		std::array<value_type, size> data;
	public:		
		BlocksArray() = default;
		explicit BlocksArray(T const value) { fill(value); }
	
		value_type       &operator[](int const index)       { assert(checkBlockIndexInChunkValid(index)); return data[index]; }
		value_type const &operator[](int const index) const { assert(checkBlockIndexInChunkValid(index)); return data[index]; }
		
		value_type       &operator[](pBlock const coord)       { assert(checkBlockCoordInChunkValid(coord)); return (*this)[blockCoordToIndex(coord)]; }
		value_type const &operator[](pBlock const coord) const { assert(checkBlockCoordInChunkValid(coord)); return (*this)[blockCoordToIndex(coord)]; }
		
		void fill(T const value) { data.fill(value); }
		void reset() { data.fill(T()); }
	};
	
	
	struct ChunkAO : CubesArray<uint8_t> {
		using CubesArray::CubesArray;
		
		static constexpr int dirsCount = 8; //8 cubes share 1 vertex
		
		static vec3i dirsForIndex(const int index) { //used in main.shader
			assert(index >= 0 && index < dirsCount); 
			const int x = int((index % 2)       != 0); //0, 2, 4, 6 - 0; 1, 3, 5, 7 - 1
			const int y = int(((index / 2) % 2) != 0); //0, 1, 4, 5 - 0; 2, 3, 6, 7 - 1
			const int z = int((index / 4)       != 0); //0, 1, 2, 3 - 0; 4, 5, 6, 7 - 1
			return vec3i{ x, y, z } * 2 - 1;
		}
	};

	
	struct ChunkLighting : CubesArray<uint8_t> {
		using CubesArray::CubesArray;
		
		static constexpr value_type maxValue = std::numeric_limits<value_type>::max();
		static constexpr value_type minValue = std::numeric_limits<value_type>::lowest();
		
		static constexpr int dirsCount{ 6 };
		
		static constexpr bool checkIndexValid(uint8_t const index) {
			return index < dirsCount;
		}
		
		static constexpr bool checkDirValid(vec3i const dir) {
			return vec3i(dir.notEqual(0)).dot(1) == 1 && dir.abs().equal(1).any();
		}
		
		static constexpr vec3i indexAsDir(uint8_t neighbourIndex) {
			assert(checkIndexValid(neighbourIndex));
			vec3i const dirs[] = { vec3i{-1,0,0},vec3i{1,0,0},vec3i{0,-1,0},vec3i{0,1,0},vec3i{0,0,-1},vec3i{0,0,1} };
			return dirs[neighbourIndex];
		}
		static uint8_t dirAsIndex(vec3i const dir) {
			assert(checkDirValid(dir));
			auto const result{ (dir.x+1)/2 + (dir.y+1)/2+abs(dir.y*2) + (dir.z+1)/2+abs(dir.z*4) };
			if(indexAsDir(result) != dir) {
				std::cerr << "err: " << dir << ' ' << result << '\n';
				assert(false);
			}
			return result; 
		}
	};
	
	
	struct LiquidCube {
		using level_t = uint8_t;
		static constexpr level_t maxLevel = std::numeric_limits<level_t>::max();
		static constexpr level_t minLevel = std::numeric_limits<level_t>::lowest();
		
		Block::id_t id;
		level_t level;
		
		LiquidCube() = default;
		//LiquidCube(Block::id_t const id_, level_t level_) : id{id_}, level{level_} { if(level == 0) id = 0; }	
		LiquidCube(Block::id_t const id_, int level_) //integer promotion
		: id{id_}, level(level_) {  
			assert(level_ >= minLevel && level <= maxLevel); 
			if(level == 0) id = 0; 
		} 
		
		bool isEmpty() const { return id == 0; }
		
		friend bool operator==(LiquidCube const f, LiquidCube const s) {
			return f.id == s.id && f.level == s.level;
		}
		friend bool operator!=(LiquidCube const f, LiquidCube const s) { return !(f == s); }
	};
	static_assert(sizeof(LiquidCube) == 4);
	
	struct ChunkLiquid : CubesArray<LiquidCube> { using CubesArray::CubesArray; };
	
	struct BlockData { //used in main.frag
		static_assert(pos::cubesInBlockCount == 8);
		uint8_t solidCubes;
		uint8_t liquidCubes;
		
		uint16_t noNeighbours : 1;
		uint16_t fullSameLiquid : 1; //if no solid cubes and all liquid cubes have the same id and level == chunk::LiquidCube::maxLevel
		uint16_t neighboursFullSameLiquid : 1;
		
		constexpr bool noCubes() const {
			return solidCubes == 0 && liquidCubes == 0;
		}		
		constexpr bool isEmpty() const {
			return noCubes();
		}		
		constexpr bool isFull() const {
			return solidCubes == 0xffu && liquidCubes == 0xffu;
		}
	};
	static_assert(sizeof(BlockData) == 4);
	
	struct BlocksData : BlocksArray<BlockData> { using BlocksArray::BlocksArray; };
	
	struct ChunkBlocksList {
		using value_type = int16_t; static_assert(pos::blocksInChunkCount < std::numeric_limits<value_type>::max());
	private:
		std::vector<value_type> list;
	public:
		ChunkBlocksList() = default;
		
		int size() const { return list.size(); }
		bool inRange(int const index) const { return index >= 0 && index < size(); }
		
		value_type       &operator[](int const index)       { assert(inRange(index)); return list[index]; }
		value_type const &operator[](int const index) const { assert(inRange(index)); return list[index]; }	
		
		vec3i operator()(int const index) const { assert(inRange(index)); return indexBlock(list[index]); }
		
		decltype(auto) begin() { return list.begin(); } 
		decltype(auto) end  () { return list.end();   }
		
		decltype(auto) cbegin() const { return list.cbegin(); }
		decltype(auto) cend  () const { return list.cend();   }
		
		void add(vec3i const blockCoord) {
			auto const newElement{ blockIndex(blockCoord) };
			assert(std::find(begin(), end(), newElement) == end());
			list.push_back(newElement);
		}
		
		void remove(vec3i const blockCoord) {
			auto const result{ std::find(begin(), end(), blockIndex(blockCoord)) };
			assert(result != end());
			list.erase(result);
		}
		
		void clear() {
			list.clear();
		}
	};
	
	struct ChunkData : BlocksArray<Block> {
		using BlocksArray::BlocksArray;
		using BlocksArray::operator[];
		
		struct Cube{ Block block; bool isSolid; };
		
		value_type       &operator[](vec3i const coord)       { return (*this)[pBlock{coord}]; }
		value_type const &operator[](vec3i const coord) const { return (*this)[pBlock{coord}]; }
		
		Cube cubeAt(vec3i const cubeCoord) const {
			auto const blockInChunkCoord{ cubeCoord / units::cubesInBlockDim };
			auto const cubeInBlockCoord { cubeCoord % units::cubesInBlockDim };
			
			auto const block{ (*this)[blockInChunkCoord] };
			auto const isCube{ block.cube(cubeInBlockCoord) };
			
			return { block, isCube };
		}
		
		Cube cubeAt(pCube const cubeCoord) const { return cubeAt(cubeCoord.val()); }
		
		Block::id_t cubeAt2(pCube const coord) const { 
			auto const cube{ cubeAt(coord) }; 
			return cube.block.id() * cube.isSolid;
		}
	};
	
	struct Chunk3x3BlocksList {
		static int constexpr sidelength = 3 * units::blocksInChunkDim;
		static_assert((sidelength-1)*(sidelength-1)*(sidelength-1) >= (1<<16), "sadly, we can't store block coords in 27 edjecent chunks in an unsigned short");
		static_assert((sidelength-1)*(sidelength-1)*(sidelength-1) <  (1<<17), "but we can store the coordinate in 17 bits");
		
		static int constexpr capacity = 30;
		
		bool checkCoordValid(vec3i const coord) {
			return coord.in(-units::blocksInChunkDim, units::blocksInChunkDim + units::blocksInChunkDim-1).all();
		}
		
		static uint32_t coordToIndex(vec3i const coord) {
			return (coord.x + units::blocksInChunkDim)
				 + (coord.y + units::blocksInChunkDim) * sidelength
				 + (coord.z + units::blocksInChunkDim) * sidelength * sidelength;
		}
		static vec3i indexToCoord(uint32_t const index) {
			return vec3i(
				index % sidelength,
				(index / sidelength) % sidelength,
				index / sidelength / sidelength
			) - units::blocksInChunkDim;
		}
	private:

		
		uint32_t bits; /*2 bits for capacity: 00 - empty, 01 - one emitter, 10 - 2+ emitters, 11 - nothing; 30 bits for the last bit of all the coords*/
		std::array<uint16_t, capacity> coords16;
	public:
		bool isEmpty() const { return bool((bits & 1) == 0); }
		
		int32_t operator[](int const position) const { return int32_t(coords16[position]) | (int32_t((bits >> (position+2)) & 1) << 16); }
		vec3i operator()(int const position) const { return indexToCoord((*this)[position]); }
		
		void clear() { bits = 0; }
		void fillRepeated(std::array<vec3i, capacity> const &coords, int const count) {
			assert(count >= 0);
			if(count == 0) { clear(); return; }
			
			uint32_t const bitsCap{ count == 1 ? 0b01u : 0b10u };
			
			uint32_t coordsBits{ 0 };
			for(int i{}; i < capacity; i++) {
				auto const blockIndex{ coordToIndex(coords[i % count]) };
				coords16[i] = uint16_t(blockIndex);
				coordsBits = coordsBits | (((blockIndex >> 16)&1) << i);
			}
			
			bits = bitsCap | (coordsBits << 2);
		}
	};
	static_assert(sizeof(Chunk3x3BlocksList) == sizeof(uint16_t) * 32);
	
	using chunkIndex_t = int32_t;
	struct ChunkAndCube {
		chunk::chunkIndex_t chunkIndex;
		chunk::CubeInChunkIndex cubeIndex;
		
		constexpr bool operator==(ChunkAndCube const it) const  noexcept {
			return chunkIndex == it.chunkIndex && cubeIndex == it.cubeIndex;
		}	
		
		constexpr bool operator<(ChunkAndCube const it) const noexcept {
			if(chunkIndex < it.chunkIndex) return true;
			else if(chunkIndex > it.chunkIndex) return false;
			else return cubeIndex < it.cubeIndex;
		}
	};
	
	struct ChunksLiquidCubes {
	private:
		static constexpr int gensCount = 2;
		chunk::Chunks *chunks_;
		std::vector<chunk::ChunkAndCube> gens[gensCount];
		int genIndex;
		
		chunk::Chunks &chunks() { return *chunks_; } 
	public:
		ChunksLiquidCubes(chunk::Chunks &chunks) : 
			chunks_{ &chunks }, gens{}, genIndex{} {}
		
		void update();
		
		void add(chunk::ChunkAndCube const cube) {
			gens[genIndex].push_back(cube);
		}
	};

	struct Chunks {	
		using index_t = chunkIndex_t;
	private:
		std::vector<index_t> vacant{};
		std::vector<index_t> used_{};
		
		struct PosHash { 
			inline std::size_t operator()(vec3i const &it) const noexcept { 
				return  (std::hash<int32_t>{}(it.x) ^ (std::hash<int32_t>{}(it.x) << 1)) ^ (std::hash<int32_t>{}(it.z) << 1);
			} 
		};
		
		std::vector<index_t> used{};
	public:
		std::vector<vec3i> chunksPos{};
		std::vector<Area> chunksAABB{};
		std::vector<ChunkStatus> chunksStatus{};
		std::vector<bool> modified{};
		std::vector<BlocksData> blocksData{};
		std::vector<ChunkData> chunksData{};
		std::vector<ChunkLiquid> chunksLiquid{};
		std::vector<ChunkAO> chunksAO{};
		std::vector<ChunkLighting> chunksSkyLighting{};
		std::vector<ChunkLighting> chunksBlockLighting{};
		std::vector<ChunkBlocksList> chunksEmitters{};
		std::vector<Neighbours> chunksNeighbours{};
		std::vector<Chunk3x3BlocksList> chunksNeighbouringEmitters;
		std::vector<index_t> chunksGPUIndex{};
		std::unordered_map<vec3i, index_t, PosHash> chunksIndex_position{};
		
		ChunksLiquidCubes liquidCubes{ *this };
	
		std::vector<index_t> const &usedChunks() const { return used; }
		
		Chunks() = default;
		Chunks(Chunks const&) = delete; //prevent accidental pass-by-value
		Chunks &operator=(Chunks const&) = delete;
		
		//returns used[] position
		inline index_t reserve() {
			index_t index;
			auto usedSize = used.size();
	
			if(!vacant.empty()) { 
				index = vacant[vacant.size()-1];
				vacant.pop_back();
			}
			else { //TODO: avoid zero-init, allocate everything at once
				index = usedSize;
				
				chunksPos.resize(index+1);
				chunksAABB.resize(index+1);
				chunksStatus.resize(index+1);
				modified.resize(index+1);
				blocksData.resize(index+1);
				chunksData.resize(index+1);
				chunksLiquid.resize(index+1);
				chunksAO.resize(index+1);
				chunksSkyLighting.resize(index+1);
				chunksBlockLighting.resize(index+1);
				chunksEmitters.resize(index+1);
				chunksNeighbours.resize(index+1);
				chunksNeighbouringEmitters.resize(index+1);
				chunksGPUIndex.resize(index+1);
			}
			used.push_back(index);
			
			return usedSize;
		}
	
		inline void recycle(index_t const index) {
			auto chunkIndex = used[index];
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
		
		inline Chunk operator[](index_t chunkIndex) {
			return Chunk{ *this, chunkIndex };
		}
	};
	
	
	struct Move_to_neighbour_Chunk {
	public:
		static bool diagonalNeighbourDirValid(vec3i const dir) {
			return (dir.abs() <= vec3i{1}).all() && dir.abs().equal(1).any();
		}
	private:
		Chunk chunk;
		bool valid;
	public:
		Move_to_neighbour_Chunk() = delete;
		
		Move_to_neighbour_Chunk(Chunks &chunks, OptionalChunkIndex oci) :
			chunk{ chunks[oci.get()] }, /*index -1 may be out of bounds but we need to keep Chunks&*/
			valid{ oci.is() }
		{}
		Move_to_neighbour_Chunk(Chunks &chunks) : chunk{chunks[0]}, valid{ false } {}/*index 0 may be out of bounds but we need to keep Chunks&*/
		Move_to_neighbour_Chunk(Chunk const src) : chunk{src}, valid{ true } {}
			
		Move_to_neighbour_Chunk(Chunks &chunks, vec3i const chunkCoord) {
			auto const chunkIndexP{ chunks.chunksIndex_position.find(chunkCoord) };
			
			valid = chunkIndexP != chunks.chunksIndex_position.end();
			
			if(valid) chunk = chunks[chunkIndexP->second];
			else chunk = chunks[0]; //index 0 may be out of bounds but we need to keep Chunks&
		}
		
		OptionalChunkIndex optChunk() const {
			if(valid) return OptionalChunkIndex{ chunk.chunkIndex() };
			return {};
		}
		
		OptionalChunkIndex move(vec3i const otherChunk) {
			if(valid) {
				auto const dir{ otherChunk - chunk.position() };
				if(otherChunk == chunk.position()) return optChunk();
				if(Neighbours::checkDirValid(dir)) return offset(dir);
				if(diagonalNeighbourDirValid(dir)) return offsetDiagonal(dir);
			}
			*this = Move_to_neighbour_Chunk(chunk.chunks(), otherChunk);
			return optChunk();
		}
		
		OptionalChunkIndex moveToNeighbour/*offset to neighbour*/(vec3i const neighbour) {
			if(!valid) return {};
			if(neighbour == 0) return optChunk();
			if(Neighbours::checkDirValid(neighbour)) return offset(neighbour);
			if(diagonalNeighbourDirValid(neighbour)) return offsetDiagonal(neighbour);
			*this = Move_to_neighbour_Chunk(chunk.chunks(), chunk.position() + neighbour);
			return optChunk();
		}
		

		OptionalChunkIndex offset/*to immediate neighbour*/(vec3i const dir) {
			if(!valid) return {};
			if(dir == 0) return optChunk();
			
			auto const optChunkIndex{ chunk.neighbours()[dir] };
			valid = optChunkIndex.is();
			if(valid) chunk = chunk.chunks()[optChunkIndex.get()];
			return optChunkIndex;
		}
		
		OptionalChunkIndex offsetDiagonal(vec3i const dir) {
			if(dir == 0) return { chunk.chunkIndex() };
			if(!valid) return {};
			assert(diagonalNeighbourDirValid(dir));
			
			OptionalChunkIndex outChunkIndex{ chunk.chunkIndex() };
			auto &chunks{ chunk.chunks() };
			if(dir.x != 0) outChunkIndex = Move_to_neighbour_Chunk{ chunks, outChunkIndex }.offset(vec3i(dir.x,0,0));
			if(dir.y != 0) outChunkIndex = Move_to_neighbour_Chunk{ chunks, outChunkIndex }.offset(vec3i(0,dir.y,0));
			if(dir.z != 0) outChunkIndex = Move_to_neighbour_Chunk{ chunks, outChunkIndex }.offset(vec3i(0,0,dir.z));
			return outChunkIndex;
		}
		
		bool is() const { return valid; }
	};
	
	//same as Move_to_neighbour_Chunk but better
	struct MovingChunk {
	public:
		static bool diagonalNeighbourDirValid(vec3i const dir) {
			return (dir.abs() <= vec3i{1}).all() && dir.abs().equal(1).any();
		}
	private:
		Chunks *chunks_;
		OptionalChunkIndex chunkIndex;
	public:
		MovingChunk() = delete;
		
		MovingChunk(Chunks &chunks__, OptionalChunkIndex chunkIndex_ = {}) : chunks_{ &chunks__ }, chunkIndex{ chunkIndex_ } {}
		MovingChunk(Chunk src) : chunks_{ &src.chunks() }, chunkIndex{ src.chunkIndex() } {}
		MovingChunk(Chunks &chunks__, vec3i const chunkCoord) : chunks_{ &chunks__ } {
			auto const chunkIndexP{ chunks_->chunksIndex_position.find(chunkCoord) };
			
			bool const valid{ chunkIndexP != chunks_->chunksIndex_position.end() };
			
			if(valid) chunkIndex = { chunkIndexP->second };
			else chunkIndex = {};
		}
		
		Chunks &chunks() const { return *chunks_; }
		OptionalChunkIndex getIndex() const { return chunkIndex; }
		bool is() const { return chunkIndex.is(); }
		Chunk get() const { assert(is()); return chunks()[chunkIndex.get()]; }
		
		MovingChunk moved(vec3i const otherChunkCoord) const {
			if(is()) {
				auto const dir{ otherChunkCoord - get().position() };
				if(dir == 0) return { *this };
				if(Neighbours::checkDirValid(dir)) return offsetedToImmediate(dir);
				if(diagonalNeighbourDirValid(dir)) return offsetedDiagonal(dir);
			}
			return MovingChunk(chunks(), otherChunkCoord);
		}
		
		MovingChunk offseted(vec3i const dir) const {
			if(!is()) return { chunks() };
			if(dir == 0) return { *this };
			if(Neighbours::checkDirValid(dir)) return offsetedToImmediate(dir);
			if(diagonalNeighbourDirValid(dir)) return offsetedDiagonal(dir);
			return MovingChunk{ chunks(), get().position() + dir };
		}

		MovingChunk offsetedToImmediate(vec3i const dir) const {
			if(!is()) return { chunks() };
			if(dir == 0) return { *this };
			
			return { chunks(), get().neighbours()[dir] };
		}
		
		MovingChunk offsetedDiagonal(vec3i const dir) const {
			if(!is()) return { chunks() };
			if(dir == 0) return { *this };
			assert(diagonalNeighbourDirValid(dir));
			
			auto  outChunk{ *this };
			if(dir.x != 0) outChunk = outChunk.offsetedToImmediate(vec3i(dir.x,0,0));
			if(dir.y != 0) outChunk = outChunk.offsetedToImmediate(vec3i(0,dir.y,0));
			if(dir.z != 0) outChunk = outChunk.offsetedToImmediate(vec3i(0,0,dir.z));
			return outChunk;
		}		
	};
}