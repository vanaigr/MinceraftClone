#pragma once

#include <type_traits>
#include <stdint.h>
#include <cassert>
#include<iostream>

namespace misc {
    constexpr double pi = 3.141592653589793238462643383279502884;
	
	template<typename T>
	void printBinary(std::ostream &o, T const it_) {
		static constexpr auto bits = sizeof(T) * 8;
		
		typename std::make_unsigned<T>::type it{ it_ };
		o << "0b";
		for(int i{bits-1}; i >= 0; i--) {
			auto const bit{ (it / (1 << i)) % 2 };
			o << (bit ? '1' : '0');
			if((i % 8) == 0 && i != 0 && i != bits-1) o << '\'';
		}
	}

    template<class T>
    constexpr T lerp(T a, T b, T f) noexcept {
        return a + f * (b - a);
    }

    template<class T>
    inline constexpr T unlerp(T a, T b, T f) noexcept {
        return (f - a) / (b - a);
    }

    template <typename E>
    constexpr inline typename std::underlying_type<E>::type to_underlying(const E e) noexcept {
        return static_cast<typename std::underlying_type<E>::type>(e);
    }
	
	template<typename T>
	T mod(T x, T y); 
	
	
    inline float modf(const float x, const float y) noexcept {
        return x - static_cast<double>(y) * floor(x / static_cast<double>(y));
    }
	template<> inline float mod<float>(float x, float y) {return modf(x,y);}
	
	inline double modd(const double x, const double y) noexcept {
        return x - y * floor(x / y);
    }
	template<> inline double mod<double>(double x, double y) {return modf(x,y);}

	template<>
    inline int32_t mod<int32_t>(int32_t x, int32_t y) {
		return ((x % y) + y) % y;
    }
	
	template<>
	inline int64_t mod<int64_t>(int64_t x, int64_t y) {
		return ((x % y) + y) % y;
    }

    template<class Type>
    inline constexpr const Type& max(const Type& t1, const Type& t2) {
        if (t1 >= t2) {
            return t1;
        }
        return t2;
    }

    template<class Type>
    inline constexpr const Type& min(const Type& t1, const Type& t2) {
        if (t1 <= t2) {
            return t1;
        }
        return t2;
    }
	
	template<typename T>
	inline constexpr T roundUpTo(T number, T round) {
        const auto remainder = (number - 1) % round;
        const auto result = number + (round - remainder - 1);
		if((result % round != 0) || (result < number)) { std::cerr << number << ' ' << round << ' ' << result << '\n'; assert(false); }
        return result;
    }

	template<typename T>
    inline constexpr T roundDownTo(T number, T round) {
        const auto remainder = misc::mod(number, round);
        const auto result = number - remainder;
		if((result % round != 0) || (result > number)) { std::cerr << number << ' ' << round << ' ' << result << '\n'; assert(false); }
        return result;
    }

	inline constexpr uint32_t roundUpIntTo(uint32_t number, uint32_t round) {
        return roundUpTo<int32_t>(number, round);
    }

    inline constexpr int32_t roundDownIntTo(int32_t number, int32_t round) {
        return roundDownTo<int32_t>(number, round);
    }
	
	inline constexpr int positiveSign(bool const positive) { return (positive ? 1 : -1); }
	inline constexpr int signPositive(int const sign) { assert(sign == 1 || sign == -1); return sign == 1; }
	
	template<typename T>
	inline constexpr T divCeil(T number, T round) {
        return roundUpTo(number, round) / round;
    }

	template<typename T>
    inline constexpr T divFloor(T number, T round) {
        return roundDownTo(number, round) / round;
    }

    template<class T>
    inline constexpr T clamp(const T value, const T b1, const T b2) {
        if (b1 > b2) return min(b1, max(value, b2));
        else return min(b2, max(value, b1));
    }

    template<class T>
    inline constexpr T map(const T value, const T va, const T vb, const T a, const T b) {
        return lerp(a, b, unlerp(va, vb, value));
    }
	
	template<typename T, size_t size, typename Action, typename = std::enable_if_t<(size>=1)>>
	inline constexpr T fold(T &&start, T (&arr)[size], Action &&a) {
		decltype(a(start, start)) accum{ start };
		for(size_t i{}; i < size; ++i) {
			accum = a(accum, arr[i]);
		}
		return accum;
	}
		
	template<typename T>
	constexpr inline T nonan(T val) {
		return (val == val) ? val : T(0);
	}
	
	template<typename T>
	constexpr inline T mix(T const from, T const to, T const factor) {
		return from * (T(1) - factor) + to * factor;
	}
	
	template<typename El, size_t r1, size_t c1r2, size_t c2>
	inline constexpr void matMult(El const (&m1)[r1][c1r2], El const (&m2)[c1r2][c2], El (*o)[r1][c2]) {
		for(size_t c = 0; c < c2; c++) {
			for(size_t r = 0; r < r1; r++) {
				El sum(0);
				for(size_t cr = 0; cr < c1r2; cr++)
					sum += m1[r][cr] * m2[cr][c];
				(*o)[r][c] = sum;
			}
		}
	}
	
	template<typename V>
	inline constexpr bool in(V const v, V const b1, V const b2) {
		return (b1 < b2) ? (b1 <= v && v <= b2) : (b2 <= v && v <= b1);
	}
	
	template<typename V>
	inline constexpr bool inX(V const v, V const b1, V const b2) {
		return (b1 < b2) ? (b1 < v && v < b2) : (b2 < v && v < b1);
	}
	
	template<typename V>
	inline constexpr bool intersects(V const v1, V const v2, V const b1, V const b2) {
		V const vi{ std::min(v1, v2) }; //mIn
		V const va{ std::max(v1, v2) }; //mAx
		V const bi{ std::min(b1, b2) };
		V const ba{ std::max(b1, b2) };
		
		return vi <= ba && bi <= va;
	}
	
	template<typename V>
	inline constexpr bool intersectsX(V const v1, V const v2, V const b1, V const b2) {
		V const vi{ std::min(v1, v2) };
		V const va{ std::max(v1, v2) };
		V const bi{ std::min(b1, b2) };
		V const ba{ std::max(b1, b2) };
		
		return vi < ba && bi < va;
	}
	
	template<typename V>
	inline constexpr bool inOtherRange(V const v1, V const v2, V const b1, V const b2) {
		V const vi{ std::min(v1, v2) };
		V const va{ std::max(v1, v2) };
		V const bi{ std::min(b1, b2) };
		V const ba{ std::max(b1, b2) };
		
		return bi <= vi && va <= ba;
	}
	
	template <typename T> 
	inline constexpr int sign(T val) {
		return (T(0) < val) - (val < T(0));
	}
}