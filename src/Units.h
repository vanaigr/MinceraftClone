#pragma once

#include"Misc.h"

#include<limits>
#include<stdint.h>
#include<utility>
#include<stdexcept>

namespace units {
	/*note: constexpr is important here because it disallows int and long long overflows and incorrect shifts, so all the constants below are correct*/
	
	//constans until the mark are copied into main.shader
	  //block is 1/16 of a chunk
	  constexpr int blocksInChunkDimAsPow2 = 4;
	  constexpr int blocksInChunkDim = 1 << blocksInChunkDimAsPow2;
	  constexpr int blocksInChunkCount = blocksInChunkDim*blocksInChunkDim*blocksInChunkDim;
	  
	  //cube is 1/2 of a block
	  constexpr int cubesInBlockDimAsPow2 = 1;
	  constexpr int cubesInBlockDim = 1 << cubesInBlockDimAsPow2;
	  constexpr int cubesInBlockCount = cubesInBlockDim*cubesInBlockDim*cubesInBlockDim;
	  
	  constexpr int cubesInChunkDimAsPow2 = cubesInBlockDimAsPow2 + blocksInChunkDimAsPow2;
	  constexpr int cubesInChunkDim = 1 << cubesInChunkDimAsPow2;
	  constexpr int cubesInChunkCount = cubesInChunkDim*cubesInChunkDim*cubesInChunkDim;

	//fractional is 1/(2^32) of a chunk
	constexpr int fracInChunkDimAsPow2 = 32;
	constexpr long long fracInChunkDim = 1ll << fracInChunkDimAsPow2;
	/*no fracInChunkCount*/
	
	constexpr int fracInBlockDimAsPow2 = fracInChunkDimAsPow2 - blocksInChunkDimAsPow2; static_assert(fracInBlockDimAsPow2 >= 0);
	constexpr int fracInBlockDim = 1 << fracInBlockDimAsPow2;
	/*no fracInBlockCount*/
	
	constexpr int fracInCubeDimAsPow2 = fracInChunkDimAsPow2 - cubesInChunkDimAsPow2; static_assert(fracInCubeDimAsPow2 >= 0);
	constexpr int fracInCubeDim = 1 << fracInCubeDimAsPow2;
	/*no fracInCubeCount*/
	
	namespace {
		template<typename... Args>
		struct TypeList;
		  template<> struct TypeList<> {};
		  using EmptyTypeList = TypeList<>;
		
		  template<typename H, typename T>
		  struct TypeList<H, T> {
		  	  using Head = H;
		  	  using Tail = T;
		  };
		
		template<typename...> struct ConstructList { using type = EmptyTypeList; };
		template<typename H, typename... T> struct ConstructList<H, T...> { using type = TypeList<H, typename ConstructList<T...>::type>; };
		
		template<typename Item, typename TypeList> struct Find;
		  template<typename Item, typename Head, typename Tail> struct Find<Item, TypeList<Head, Tail>> 
		      { static constexpr bool value = std::is_same_v<Item, Head> || Find<Item, Tail>::value; };
		  template<typename Item> struct Find<Item, EmptyTypeList> { static constexpr bool value = false; };
		
		//c++ 20 https://en.cppreference.com/w/cpp/types/remove_cvref
		template< class T > struct remove_cvref { typedef std::remove_cv_t<std::remove_reference_t<T>> type; };
		template< class T > using remove_cvref_t = typename remove_cvref<T>::type;
		
		template<typename From, typename To, typename Void = void> struct CanCast;// { static constexpr bool value; };
		template<typename From, typename To, typename Void = void> struct IsCastExplicit;// { static constexpr bool value = /should cast From -> To be explicit/; };
		template<typename From, typename To, typename Void = void> struct CommonType;// { using type = common type between units From and To; };
		template<typename From, typename To, typename Void = void> struct Cast;// { constexpr static To castUnit(From); };
		//	typename Void is for forward declaring class templates in anonymous namespace. 
		//	Maybe there is a better way for declaring but not defining class templates
		//	that are visible only to the namespace members themselves
		
		template<typename From, typename To> struct HaveCommon {
			using common = typename CommonType<From, To>::type;
			static constexpr bool value = CanCast<From, common>::value && CanCast<To, common>::value;
		};
	}
	
	
	template<typename ID_, typename value_type_, value_type_ min_, value_type_ max_>
	struct Unit {
		using This = Unit<ID_, value_type_, min_, max_>;
		using ID = ID_;
		using value_type = value_type_;
		static constexpr value_type min = min_;
		static constexpr value_type max = max_;		
	private:
		value_type value_;
	public:
		//constexpr Unit() = default; //0 may be outside of [min; max]
		constexpr Unit(value_type const src) : value_(src) { if(min <= value_ && value_ <= max); else throw std::logic_error(""); }	
		
		template<typename Other, typename = std::enable_if_t<
			!std::is_same_v<remove_cvref_t<Other>, This> //disable this constructor if argument is this type
			&& CanCast<Other, This>::value
			&& IsCastExplicit<Other, This>::value
		>> constexpr explicit Unit(Other const other) : Unit{ Cast<Other, This>::castUnit(other).value() } {
			if(min <= value_ && value_ <= max); else throw std::logic_error("");
		}
		
		template<typename Other, typename = std::enable_if_t<
			!std::is_same_v<remove_cvref_t<Other>, This> //disable this constructor if argument is this type
			&& CanCast<This, Other>::value
			&& !IsCastExplicit<This, Other>::value
		>> constexpr operator Other() const {
			return Cast<This, Other>::castUnit(*this);
		}
		
		constexpr value_type value() const { return value_; };
		
		constexpr friend This operator+(This const c1, This const c2) { return This{value_type(c1.value() + c2.value()) }; }
		constexpr friend This &operator+=(This &c1, This const c2) { return c1 = c1 + c2; }
		
		constexpr friend This operator-(This const c1,This const c2) { return This{value_type(c1.value() - c2.value()) }; }
		constexpr friend This &operator-=(This &c1, This const c2) { return c1 = c1 - c2; }

		template<typename Other, typename = std::enable_if_t<HaveCommon<This, Other>::value>> constexpr friend auto operator+(This const c1, Other const c2) {
			using common = typename CommonType<This, Other>::type;
			
			return Cast<This , common>::castUnit(c1)
				 + Cast<Other, common>::castUnit(c2);
		}
		
		#define comp(OPERATOR) constexpr friend bool operator##OPERATOR (This const c1, This const c2) { return c1.value() OPERATOR c2.value(); } \
		 \
		template<typename Other, typename = std::enable_if_t<HaveCommon<This, Other>::value>> constexpr friend bool operator##OPERATOR (This const c1, Other const c2) { \
			using common = typename CommonType<This, Other>::type; \
			 \
			return Cast<This , common>::castUnit(c1) OPERATOR Cast<Other, common>::castUnit(c2); \
		}
		
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Winvalid-token-paste"
		  comp(<)
		  comp(<=)
		  comp(>)
		  comp(>=)
		  comp(==)
		  comp(!=)
		#pragma clang diagnostic pop
		
		#undef comp
		
		template<typename T>
		constexpr static Unit fromAny(T const value) {
			if(T(min) <= value && value <= T(max)); 
			else throw std::logic_error("");
			
			return Unit(static_cast<value_type>(value));
		}
	};

	//units ID
	struct UnitFractional;
	struct UnitChunk;
	struct UnitBlock;
	struct UnitCube;

	//units
	using Fractional = Unit<UnitFractional, long long, std::numeric_limits<long long>::lowest(), std::numeric_limits<long long>::max()>;
	using Chunk = Unit<UnitChunk, int, (Fractional::min >> fracInChunkDimAsPow2), (Fractional::max >> fracInChunkDimAsPow2)>;
	using Block = Unit<UnitBlock, long long, (long long)(Chunk::min) * blocksInChunkDim, (long long)(Chunk::max) * blocksInChunkDim>;
	using Cube = Unit<UnitCube, long long, (long long)(Block::min) * cubesInBlockDim, (long long)(Block::max) * cubesInBlockDim>;
	
	//type conversions
	namespace {
		using hierarchy = typename ConstructList<Chunk, Block, Cube, Fractional>::type;
		
		template<typename T, typename Hierarchy> struct WiderTypesFor{ using type = EmptyTypeList; };
		  template<typename T, typename HierarchyHead, typename HeadWiderTypes> struct WiderTypesFor<T, TypeList<HierarchyHead, HeadWiderTypes>> {
			  using type = std::conditional_t<
				  std::is_same_v<T, HierarchyHead>,
				  HeadWiderTypes,
				  typename WiderTypesFor<T, HeadWiderTypes>::type
			  >;
		  };
				
		template<typename T> struct UnitInfo;/* { static constexpr /base class value type/ factor = conversion to base unit as a power of 2; };*/ 
		template<> struct UnitInfo<Chunk>      { static constexpr Fractional::value_type factor = fracInChunkDimAsPow2; };
		template<> struct UnitInfo<Block>      { static constexpr Fractional::value_type factor = fracInBlockDimAsPow2; };
		template<> struct UnitInfo<Cube>       { static constexpr Fractional::value_type factor = fracInCubeDimAsPow2;  };
		template<> struct UnitInfo<Fractional> { static constexpr Fractional::value_type factor = 0;                    };
	}
	
	template<typename From, typename To> struct CanCast<From, To, void> {
		static constexpr bool value = Find<From, hierarchy>::value && Find<To, hierarchy>::value;
	};
	
	template<typename From, typename To> struct IsCastExplicit<From, To, void> {
		static constexpr bool value = !(Find<To, typename WiderTypesFor<From, hierarchy>::type>::value);
	};
	
	template<typename From, typename To> struct CommonType<From, To, void> {
		using type = std::conditional_t<
			IsCastExplicit<From, To>::value,
			From,
			To
		>;
	};
	
	template<typename From, typename To> struct Cast<From, To, void> {
		constexpr static To castUnit(From const from) {
			return To::fromAny(
				(static_cast<Fractional::value_type>(from.value()) << UnitInfo<From>::factor) >> UnitInfo<To>::factor 
			);
		}
	};
	
	//helpers
	template<typename Unit, typename Other> constexpr bool fitsIn(Unit const unit, Other const other) {
		return other <= unit && unit < (other + 1);
	}
	template<typename Unit> constexpr bool fitsInOneChunk(Unit const unit) { return fitsIn<Unit, Chunk>(unit, Chunk{0}); }
	
	
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
		
		void test() {
			TestConversion<Block, 5>::to<Fractional , fracInBlockDim  * 5>::check();
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
	}
}