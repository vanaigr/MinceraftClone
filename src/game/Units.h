#pragma once
#include"Misc.h"
#include"Unit.h"

#include<stdint.h>
#include<cmath>
#include<limits>


namespace units {
	/*note: constexpr is important here because it disallows int and long long overflows and incorrect shifts, so all the constants below are correct*/
	
	//constans until the mark are copied into main.shader
	  //block is 1/16 of a chunk
	  constexpr int blocksInChunkDimAsPow2 = 4;
	  constexpr int blocksInChunkDim = 1 << blocksInChunkDimAsPow2;
	  
	  //cube is 1/2 of a block
	  constexpr int cubesInBlockDimAsPow2 = 1;
	  constexpr int cubesInBlockDim = 1 << cubesInBlockDimAsPow2;
	  
	  constexpr int cubesInChunkDimAsPow2 = cubesInBlockDimAsPow2 + blocksInChunkDimAsPow2;
	  constexpr int cubesInChunkDim = 1 << cubesInChunkDimAsPow2;

	//fractional is 1/(2^32) of a chunk
	constexpr int fracInChunkDimAsPow2 = 32;
	constexpr long long fracInChunkDim = 1ll << fracInChunkDimAsPow2;
	
	constexpr int fracInBlockDimAsPow2 = fracInChunkDimAsPow2 - blocksInChunkDimAsPow2; static_assert(fracInBlockDimAsPow2 >= 0);
	constexpr int fracInBlockDim = 1 << fracInBlockDimAsPow2;
	
	constexpr int fracInCubeDimAsPow2 = fracInChunkDimAsPow2 - cubesInChunkDimAsPow2; static_assert(fracInCubeDimAsPow2 >= 0);
	constexpr int fracInCubeDim = 1 << fracInCubeDimAsPow2;
	
	struct UnitsHierarchy;

	//units ID
	struct UnitFractional;
	struct UnitChunk;
	struct UnitBlock;
	struct UnitCube;

	//units
	using Fractional = unit::Unit<UnitsHierarchy, UnitFractional, int64_t>;
	using Chunk      = unit::Unit<UnitsHierarchy, UnitChunk     , int32_t>;
	using Block      = unit::Unit<UnitsHierarchy, UnitBlock     , int32_t>;
	using Cube       = unit::Unit<UnitsHierarchy, UnitCube      , int32_t>;

    struct UnitsHierarchy {	
        using hierarchy = typename unit::ConstructList<Chunk, Block, Cube, Fractional>::type;
        template<typename T> struct UnitInfo;
    };

    //type conversions
    template<> struct UnitsHierarchy::UnitInfo<units::Chunk>      { static constexpr Fractional::value_type baseFactor = fracInChunkDimAsPow2; };
    template<> struct UnitsHierarchy::UnitInfo<units::Block>      { static constexpr Fractional::value_type baseFactor = fracInBlockDimAsPow2; };
    template<> struct UnitsHierarchy::UnitInfo<units::Cube>       { static constexpr Fractional::value_type baseFactor = fracInCubeDimAsPow2;  };
    template<> struct UnitsHierarchy::UnitInfo<units::Fractional> { static constexpr Fractional::value_type baseFactor = 0;                    };

	//helpers
	template<typename Unit, typename Other> constexpr bool fitsIn(Unit const unit, Other const other) {
		return other <= unit && unit < (other + 1);
	}
	template<typename Unit> constexpr bool fitsInOneChunk(Unit const unit) { return fitsIn<Unit, Chunk>(unit, Chunk{0}); }
	
	
	inline Fractional posToFrac(double value) { return Fractional::create(floor(value*fracInBlockDim)); }
	inline Fractional posToFracTrunk(double value) { return Fractional::create(value*fracInBlockDim/*default is truncate*/); }
	inline Fractional posToFracRAway(double value) {  //round away from zero
		auto const val{ value*fracInBlockDim };
		return Fractional::create( std::copysign(ceil(abs(val)), val) ); 
	}
	
	static constexpr inline double fracToPos(Fractional value) { return static_cast<double>(value.value()) / fracInBlockDim; }
	
	//more constants
	constexpr Cube::value_type cubeMin( std::numeric_limits<Cube::value_type>::lowest() );
	constexpr Cube::value_type cubeMax( std::numeric_limits<Cube::value_type>::max() );
	
	constexpr Block::value_type blockMin( cubeMin >> cubesInBlockDimAsPow2 );
	constexpr Block::value_type blockMax( cubeMax >> cubesInBlockDimAsPow2 );
	
	constexpr Chunk::value_type chunkMin( blockMin >> blocksInChunkDimAsPow2 );
	constexpr Chunk::value_type chunkMax( blockMax >> blocksInChunkDimAsPow2 );
	
	constexpr Fractional::value_type fracMin( Fractional::value_type{chunkMin} * fracInChunkDim );
	constexpr Fractional::value_type fracMax( Fractional::value_type{chunkMax} * fracInChunkDim );
	
	namespace /*test*/ {
		template<typename T1, typename T1::value_type V1, typename T2, typename T2::value_type V2>
		struct TestSum {
			template<typename Result, typename Result::value_type ResultValue> 
			struct equals {
				static constexpr void check() {
					constexpr T1 v1{ V1 };
					constexpr T2 v2{ V2 };
					
					static_assert(std::is_same_v<decltype(v1 + v2), Result>);
					static_assert((v1 + v2).value() == ResultValue);
				}
			};
		};
		
		template<typename T1, typename T1::value_type V1>
		struct TestConversion {
			template<typename Result, typename Result::value_type ResultValue> 
			struct to {
				static constexpr void check() {
					constexpr T1 v1{ V1 };
					constexpr Result result{ static_cast<Result>(v1) };
					
					static_assert(result.value() == ResultValue);
				}
			};
		};
		
		constexpr void test() {
			TestConversion<Block, 5>::to<Fractional, fracInBlockDim  * 5>::check();
			TestConversion<Block, 5>::to<Cube, cubesInBlockDim * 5>::check();
			TestConversion<Chunk, 5>::to<Cube, cubesInChunkDim * 5>::check();
			TestConversion<Fractional, 5 * fracInBlockDim>::to<Block, 5>::check();
			
			TestSum<Chunk     , 2,  Block     , 3>::equals<Block     , 2 * blocksInChunkDim + 3>::check();
			TestSum<Chunk     , 2,  Cube      , 3>::equals<Cube      , 2 * cubesInChunkDim  + 3>::check();
			TestSum<Chunk     , 2,  Fractional, 3>::equals<Fractional, 2 * fracInChunkDim   + 3>::check();
			TestSum<Fractional, 2,  Chunk     , 3>::equals<Fractional, 2 +   fracInChunkDim * 3>::check();
			TestSum<Block     , 2,  Cube      , 3>::equals<Cube      , 2 * cubesInBlockDim  + 3>::check();
			TestSum<Block     , 2,  Fractional, 3>::equals<Fractional, 2 * fracInBlockDim   + 3>::check();
			
			static_assert(fitsInOneChunk(Block{blocksInChunkDim - 1}));
			static_assert(!fitsInOneChunk(Block{blocksInChunkDim}));
		}
		static_assert((test(), true)); //supress 'unused function' test
	}
}

using uChunk = units::Chunk;
using uBlock = units::Block;
using uCube  = units::Cube;
using uFrac  = units::Fractional;
