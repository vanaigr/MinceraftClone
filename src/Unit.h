#pragma once

#include<type_traits>

namespace unit {
	template<typename... Args>
	struct TypeList;
	  template<> struct TypeList<> {};
	  using EmptyTypeList = TypeList<>;
	  
	  template<typename H, typename T> struct TypeList<H, T> {
		  using Head = H;
		  using Tail = T;
	  };
	
	template<typename...> struct ConstructList { using type = EmptyTypeList; };
	  template<typename H, typename... T> struct ConstructList<H, T...> { using type = TypeList<H, typename ConstructList<T...>::type>; };
	
	template<typename Item, typename TypeList> struct Find { static constexpr bool value = false; };
	  template<typename Item, typename Head, typename Tail> struct Find<Item, TypeList<Head, Tail>> 
	  { static constexpr bool value = std::is_same_v<Item, Head> || Find<Item, Tail>::value; };
	
	//c++ 20 https://en.cppreference.com/w/cpp/types/remove_cvref
	template< class T > struct remove_cvref { typedef std::remove_cv_t<std::remove_reference_t<T>> type; };
	template< class T > using remove_cvref_t = typename remove_cvref<T>::type;
	
	template<typename T, typename List> struct ItemTail{ using type = unit::EmptyTypeList; };
	  template<typename T, typename Head, typename Tail> struct ItemTail<T, unit::TypeList<Head, Tail>> {
		  using type = std::conditional_t<
			  std::is_same_v<T, Head>,
			  Tail,
			  typename ItemTail<T, Tail>::type
		  >;
	  };
	
	template<typename List> struct FirstElement;
	  template<typename H, typename H2, typename T> struct FirstElement<TypeList<H, TypeList<H2, T>>> { using type = typename FirstElement<TypeList<H2, T>>::type; };
	  template<typename H                         > struct FirstElement<TypeList<H, EmptyTypeList>  > { using type = H; };
	
	/*struct Hierarchy {
		using hierarchy = typename unit::ConstructList<NarrowType, WiderType, WiderType...>::type;
				
		template<typename T> struct UnitInfo { static constexpr /base class value type/ factor = conversion to base unit as a power of 2; }; 
	};*/
	
	template<typename Hierarchy, typename From, typename To> struct CanCast {
		static constexpr bool value = Find<From, typename Hierarchy::hierarchy>::value && Find<To, typename Hierarchy::hierarchy>::value;
	};
	
	template<typename Hierarchy, typename From, typename To> struct IsCastExplicit {
		static constexpr bool value = !(
			std::is_same_v<From, To> 
			|| Find< To, typename ItemTail/*list of types wider than given*/<From, typename Hierarchy::hierarchy>::type >::value
		);
	};
	
	template<typename Hierarchy, typename From, typename To> struct CommonType {
		using type = std::conditional_t<
			IsCastExplicit<Hierarchy, From, To>::value,
			From,
			To
		>;
	};
	
	template<typename Hierarchy>
	using baseType_t = typename FirstElement<typename Hierarchy::hierarchy>::type;
	
	template<typename Hierarchy, typename From, typename To> struct Cast {
		constexpr static To castUnit(From const from) {
			return To::create(
				(static_cast<typename baseType_t<Hierarchy>::value_type>(from.value())
				<< Hierarchy::template UnitInfo<From>::baseFactor)
				>> Hierarchy::template UnitInfo<To>::baseFactor 
			);
		}
	};
	
	
	template<typename Hierarchy, typename From, typename To> struct HaveCommon {
		using common = typename CommonType<Hierarchy, From, To>::type;
		static constexpr bool value = CanCast<Hierarchy, From, common>::value && CanCast<Hierarchy, To, common>::value;
	};
	
	
	template<typename Hierarchy_, typename ID_, typename value_type_>
	struct Unit {
		using This = Unit<Hierarchy_, ID_, value_type_>;
		using Hierarchy = Hierarchy_;
		using ID = ID_;
		using value_type = value_type_;	
	private:
		value_type value_;
	public:
		constexpr Unit() = default;
		constexpr Unit(value_type const src) : value_(src) {}	
		
		template<typename Other, typename = std::enable_if_t<
			!std::is_same_v<remove_cvref_t<Other>, This> //this constructor is disabled if argument is of type This
			&& CanCast<Hierarchy, Other, This>::value
			&& IsCastExplicit<Hierarchy, Other, This>::value
		>> constexpr explicit Unit(Other const other) : Unit{ Cast<Hierarchy, Other, This>::castUnit(other).value() } {}
		
		template<typename Other, typename = std::enable_if_t<
			!std::is_same_v<remove_cvref_t<Other>, This> //this constructor is disabled if argument is of type This
			&& CanCast<Hierarchy, This, Other>::value
			&& !IsCastExplicit<Hierarchy, This, Other>::value
		>> constexpr operator Other() const {
			return Cast<Hierarchy, This, Other>::castUnit(*this);
		}
		
		constexpr value_type value() const { return value_; };
		
		constexpr friend This operator+(This const c1, This const c2) { return This{value_type(c1.value() + c2.value()) }; }
		constexpr friend This &operator+=(This &c1, This const c2) { return c1 = c1 + c2; }
		
		constexpr friend This operator-(This const c1,This const c2) { return This{value_type(c1.value() - c2.value()) }; }
		constexpr friend This &operator-=(This &c1, This const c2) { return c1 = c1 - c2; }
	
		template<typename Other, typename = std::enable_if_t<HaveCommon<Hierarchy, This, Other>::value>> 
		constexpr friend auto operator+(This const c1, Other const c2) {
			using common = typename CommonType<Hierarchy, This, Other>::type;
			
			return Cast<Hierarchy, This , common>::castUnit(c1)
				 + Cast<Hierarchy, Other, common>::castUnit(c2);
		}
		
		template<typename Other, typename = std::enable_if_t<HaveCommon<Hierarchy, This, Other>::value>> 
		constexpr friend auto operator-(This const c1, Other const c2) {
			using common = typename CommonType<Hierarchy, This, Other>::type;
			
			return Cast<Hierarchy, This , common>::castUnit(c1)
				 - Cast<Hierarchy, Other, common>::castUnit(c2);
		}
		
		template<typename T>
		constexpr static This create(T &&it) {
			return This(static_cast<value_type>(it));
		}
		
		template<typename Other>
		constexpr auto as() const {
			return static_cast<Other>(*this);
		}
		
		template<typename Other>
		constexpr auto valAs() const {
			return as<Other>().value();
		}
		
		template<typename Other>
		constexpr auto in() const {
			return *this - static_cast<This>(static_cast<Other>(*this));
		}
		
		template<typename Other>
		constexpr auto valIn() const {
			return in<Other>().value();
		}
		
		#define comp(OPERATOR) constexpr friend auto operator##OPERATOR (This const c1, This const c2) { return c1.value() OPERATOR c2.value(); } \
		\
		template<typename Other, typename = std::enable_if_t<HaveCommon<Hierarchy, This, Other>::value>> constexpr friend auto operator##OPERATOR (This const c1, Other const c2) { \
			using common = typename CommonType<Hierarchy, This, Other>::type; \
			\
			return Cast<Hierarchy, This , common>::castUnit(c1) OPERATOR Cast<Hierarchy, Other, common>::castUnit(c2); \
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
	};
}
