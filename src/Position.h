#pragma once
#include"Unit.h"
#include"Units.h"
#include"Vector.h"

#include<cmath>

namespace pos {
	template<typename T> constexpr decltype(auto) cubed(T &&t) { return t*t*t; }
	
	constexpr int blocksInChunkCount = cubed( units::Block(units::Chunk{1}).value() );
	constexpr int cubesInBlockCount = cubed( units::Cube(units::Block{1}).value() );
	constexpr int cubesInChunkCount = cubed( units::Cube(units::Chunk{1}).value() );
			  
	struct UnitsHierarchy;

	//units ID
	struct UnitFractional;
	struct UnitChunk;
	struct UnitBlock;
	struct UnitCube;

	//units
	using Fractional = unit::Unit<UnitsHierarchy, UnitFractional, vec3l>;
	using Chunk      = unit::Unit<UnitsHierarchy, UnitChunk     , vec3i>;
	using Block      = unit::Unit<UnitsHierarchy, UnitBlock     , vec3i>;
	using Cube       = unit::Unit<UnitsHierarchy, UnitCube      , vec3i>;
}

//type conversions
struct pos::UnitsHierarchy {	
	using hierarchy = typename unit::ConstructList<Chunk, Block, Cube, Fractional>::type;
			
	template<typename T> struct UnitInfo;
	template<> struct UnitInfo<Chunk>      { static constexpr Fractional::value_type baseFactor = units::fracInChunkDimAsPow2; };
	template<> struct UnitInfo<Block>      { static constexpr Fractional::value_type baseFactor = units::fracInBlockDimAsPow2; };
	template<> struct UnitInfo<Cube>       { static constexpr Fractional::value_type baseFactor = units::fracInCubeDimAsPow2;  };
	template<> struct UnitInfo<Fractional> { static constexpr Fractional::value_type baseFactor = 0;                           };
};

namespace pos {
	//copies from Units.h, but for positions
	static constexpr inline Fractional posToFrac(vec3d value) { return Fractional::create((value*units::fracInBlockDim).floor()); }
	static constexpr inline Fractional posToFracTrunk(vec3d value) { return Fractional::create((value*units::fracInBlockDim).trunc()); }
	static constexpr inline Fractional posToFracRAway(vec3d value) {  //round away from zero
		auto const val{ (value*units::fracInBlockDim) };
		return Fractional::create( val.abs().ceil().applied([&](auto const coord, auto i) -> double {
			return std::copysign(coord, val[i]);
		}) ); 
	}
	
	static constexpr inline vec3d fracToPos(Fractional value) { return static_cast<vec3d>(value.value()) / units::fracInBlockDim; }
}

using pChunk = pos::Chunk;
using pBlock = pos::Block;
using pCube  = pos::Cube;
using pFrac  = pos::Fractional;