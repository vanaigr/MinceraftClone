#pragma once

#include<vector>
#include<stdint.h>
#include<array>
#include <tuple>

#include"Vector.h"

struct Chunks {
public:
	static constexpr int const chunkDim = 16;
	static constexpr int const chunkSize = chunkDim*chunkDim*chunkDim;
	using ChunkData = std::array<uint16_t, chunkSize>;
	struct ChunkDataRepresentation {
		ChunkData cd;
	};
private:
	std::vector<int> vacant{};
	std::vector<int> used_{};
	
	std::vector<int> used{};
	std::vector<vec3<int32_t>> chunksPos{};
	std::vector<ChunkDataRepresentation> chunksDataRepr{};
	std::vector<uint8_t> chunkNew{};
public:
	
	inline std::vector<int> const &usedChunks() const { return used; }
	
	inline std::vector<vec3i> &chunksPosition() { return chunksPos; }
	inline std::vector<ChunkDataRepresentation> &chunksData() { return chunksDataRepr; }
	inline std::vector<uint8_t> &isChunkNew() { return chunkNew; }
	
	inline std::vector<vec3i> const &chunksPosition() const { return chunksPos; }
	inline std::vector<ChunkDataRepresentation> const &chunksData() const { return chunksDataRepr; }
	inline std::vector<uint8_t> const &isChunkNew() const { return chunkNew; }
	
		//returns used[] position
	inline int reserve() {
		int index;
		int usedSize = used.size();

		if(!vacant.empty()) { 
			index = vacant[vacant.size()-1];
			vacant.pop_back();
		}
		else {
			index = usedSize;
			chunksPos.resize(index+1);
			chunksDataRepr.resize(index+1);
			chunkNew.resize(index+1);
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
		
		used.resize(0);
		used.swap(used_);
			
		/*for(size_t i = 0; i < sz; ++i) {
			auto const &chunkIndex = used[i];
			if(!keep(chunkIndex)) used[i]=-1;
		}
		
		int endIndex = sz-1;
		for(int i = 0; i <= endIndex; ++i) {
			if(used[i] == -1) {
                for(;endIndex > -1; --endIndex) {
                    if(used[endIndex]!=-1) {
                        if(endIndex > i)  {
							used[i] = used[endIndex];
							used[endIndex] = -1;//
							endIndex--;//for checking.   used[i] = used[endIndex--];
							
						}
                        break;
                    }	
				}					
			}
		}//produces chunk re-generation in the middle of already generated area
		
		
		
		if(true)
			for(int i = 0; i < sz; i ++) {
				if(i <= endIndex && used[i] == -1) {
					std::cout << "Bug in algorithm ==-1: i=" << i << ",endIndex=" << endIndex << ",vector:\n";
					for(int j = 0; j < sz; j ++) {
						std::cout << used[j] << ' ';
					}
					exit(-1);
				}
				else if(i > endIndex && used[i] != -1) {
					std::cout << "Bug in algorithm !=-1: i=" << i << ",endIndex=" << endIndex << ",vector:" << std::endl;
					for(int j = 0; j < sz; j ++) {
						std::cout << used[j] << ' ';
					}
					exit(-1);
				}
			}
		
		used.resize(endIndex+1);*/
	}
	
	inline static constexpr int32_t blockIndex(vec3<int32_t> position) {
		return position.x + position.y*Chunks::chunkDim + position.z*Chunks::chunkDim*Chunks::chunkDim;
	}
	
	inline static constexpr vec3<int32_t> asChunkCoord(vec3<double> position) {
		return static_cast<vec3<int32_t>>( (position / Chunks::chunkDim).appliedFunc<double(*)(double)>(floor) );
	}
	
	inline static constexpr vec3<double> inChunkCoord(vec3<double> position) {
		return position.applied([](auto const coord, auto ignore) -> auto { return misc::modd(coord, 16.0); });
	}
	
	inline static constexpr vec3<int32_t> inChunkCoord(vec3<int32_t> position) {
		return position.applied([](auto const coord, auto ignore) -> auto { return misc::mod(coord, 16); });
	}
	
	inline static constexpr std::tuple<vec3i, vec3d> normalizePos(vec3i const chunkPos, vec3d const inChunkPos) {
			auto const chunkOffset{ Chunks::asChunkCoord(inChunkPos) };
			auto const inChunkPosition{ Chunks::inChunkCoord(inChunkPos) };
			return std::make_tuple(chunkPos+chunkOffset, inChunkPosition);
	}
};