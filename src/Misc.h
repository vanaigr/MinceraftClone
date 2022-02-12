#pragma once

#include <type_traits>
#include <stdint.h>
#include <cassert>
#include<iostream>

namespace misc {
    constexpr double pi = 3.141592653589793238462643383279502884;

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

    inline float modf(const float x, const float y) noexcept {
        return x - static_cast<double>(y) * floor(x / static_cast<double>(y));
    }
	
	inline double modd(const double x, const double y) noexcept {
        return x - y * floor(x / y);
    }

    inline constexpr int32_t mod(const int32_t x, const int32_t y) noexcept {
        int32_t mod = x % y;
        // if the signs are different and modulo not zero, adjust result
        if ((x ^ y) < 0 && mod != 0) {
            mod += y;
        }
        return mod;
    }
	
	inline constexpr int64_t mod(const int64_t x, const int64_t y) noexcept {
        auto mod = x % y;
        // if the signs are different and modulo not zero, adjust result
        if ((x ^ y) < 0ll && mod != 0ll) {
            mod += y;
        }
        return mod;
    }

    inline constexpr uint32_t umod(const uint32_t x, const uint32_t y) noexcept {
        return x % y;
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

    inline constexpr uint32_t roundUpIntTo(uint32_t number, uint32_t round) {
        const auto remainder = (number - 1) % round;
        const auto result = number + (round - remainder - 1);
        assert(result % round == 0);
        return result;
    }

    inline constexpr int32_t roundDownIntTo(int32_t number, int32_t round) {
        const auto remainder = misc::mod(number, round);
        const auto result = number - remainder;
        assert((result % round == 0) || (result <= number));
        return result;
    }

    inline constexpr uint32_t intDivCeil(uint32_t number, uint32_t round) {
        return roundUpIntTo(number, round) / round;
    }

    inline constexpr int32_t intDivFloor(int32_t number, int32_t round) {
        return roundDownIntTo(number, round) / round;
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
	
	/*void invertMatrix3To(double const (&m)[3][3], double (*minv)[3][3]) {//https://stackoverflow.com/a/18504573/15291447
	double const det = 
				m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
				m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
				
				std::cout << det << '\n';
	
	double const invdet = 1.0 / det;
	
	(*minv)[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) / invdet;
	(*minv)[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / invdet;
	(*minv)[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / invdet;
	(*minv)[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / invdet;
	(*minv)[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / invdet;
	(*minv)[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) / invdet;
	(*minv)[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) / invdet;
	(*minv)[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) / invdet;
	(*minv)[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) / invdet;
	}*/
	
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
		return (b1 < b2) ? (b1 < v && v < b2) : (b2 < v && v < b1);
	}
	
	template <typename T> 
	inline constexpr int sign(T val) {
		return (T(0) < val) - (val < T(0));
	}
}