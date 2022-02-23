#pragma once

#include<vector>
#include<stdint.h>
#include<array>
#include <tuple>

#include"Vector.h"

struct Chunks {
public:
	static constexpr int const chunkDimAsPow2 = 4;
	static constexpr int const chunkDim = 1 << chunkDimAsPow2;
	static constexpr int const chunkSize = chunkDim*chunkDim*chunkDim;
	//static constexpr int const tmpChunkSize = chunkDim*chunkDim*chunkDim/(sizeof(uint16_t) * 8);
	using ChunkData = std::array<uint16_t, chunkSize>;
	//using tmpChunkData = std::array<uint8_t, tmpChunkSize>;
private:
	std::vector<int> vacant{};
	std::vector<int> used_{};
	
	std::vector<int> used{};
	std::vector<vec3<int32_t>> chunksPos{};
	std::vector<bool> gpuPresent_{};
	std::vector<ChunkData> chunksDataRepr{};
	//std::vector<tmpChunkData> tmpChunksData{};
public:
	
	inline std::vector<int> const &usedChunks() const { return used; }
	
	inline std::vector<vec3i> &chunksPosition() { return chunksPos; }
	inline std::vector<bool> &gpuPresent() { return gpuPresent_; }
	inline std::vector<ChunkData> &chunksData() { return chunksDataRepr; }
	//inline std::vector<ChunkDataRepresentation> &temporalChunksData() { return tmpChunksData; }
	
	inline std::vector<vec3i>const &chunksPosition() const { return chunksPos; }
	inline std::vector<bool>const &gpuPresent() const { return gpuPresent_; }
	inline std::vector<ChunkData>const &chunksData() const { return chunksDataRepr; }
	//inline std::vector<ChunkDataRepresentation>const &temporalChunksData() const { return tmpChunksData; }
	
	//returns used[] position
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
			gpuPresent_.resize(index+1);
			chunksDataRepr.resize(index+1);
		}
		used.push_back(index);
		
		return usedSize;
	}

	inline void recycle(int const index) {
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
	
	template<typename Predicate>
	inline void filterUsed(Predicate&& keep) {
		auto const sz = used.size();
		
		for(size_t i = 0; i < sz; ++i) {
			auto const &chunkIndex = used[i];
			if(keep(chunkIndex)) used_.push_back(chunkIndex);
			else vacant.push_back(chunkIndex);
		}
		
		used.clear();
		used.swap(used_);
	}
	
	inline static constexpr int32_t blockIndex(vec3<int32_t> position) {
		return position.x + position.y*Chunks::chunkDim + position.z*Chunks::chunkDim*Chunks::chunkDim;
	}
};