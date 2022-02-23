#include"Vector.h"
#include"Misc.h"
#include<type_traits>

struct Viewport {
	private:
	template<typename El, size_t i_, size_t j_> 
	static inline void copy2DArray(El const (&in)[i_][j_], El (&out)[i_][j_]) {
		for(size_t i = 0; i < i_; ++i) {
			for(size_t j = 0; j < j_; ++j) {
				out[i][j] = in[i][j];
			}
		}
	}
	public:
	
    vec2<double> rotation{};
	double aspectRatio;// height / width
	double fov;
	double near, far;

    constexpr vec3<double> rightDir() const {
        return vec3<double>(cos(rotation.x), 0, sin(rotation.x));
    }
    constexpr vec3<double> topDir() const {
        return vec3<double>(-sin(rotation.x) * sin(rotation.y), cos(rotation.y), cos(rotation.x) * sin(rotation.y));
    }
    constexpr vec3<double> forwardDir() const {
        return topDir().cross(rightDir());
		//return vec3<double>(cos(rotation.y) * sin(rotation.x), sin(rotation.y), -cos(rotation.y) * cos(rotation.x));
    }
	
	constexpr vec3<double> flatRightDir() const {
        return vec3<double>(cos(rotation.x), 0, sin(rotation.x));
    }
    constexpr vec3<double> flatTopDir() const {
        return vec3<double>(0, 1, 0);
    }
    constexpr vec3<double> flatForwardDir() const {
        return flatTopDir().cross(flatRightDir());
		//return vec3<double>(cos(rotation.y) * sin(rotation.x), sin(rotation.y), -cos(rotation.y) * cos(rotation.x));
    }
	
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void localToGlobalSpace(O (*mat_out)[3][3]) const {
		auto const rd = rightDir();
		auto const td = topDir();
		auto const fd = forwardDir();
		
		O const pm[3][3] = {
			{ static_cast<O>(rd.x), static_cast<O>(td.x), static_cast<O>(fd.x) },
			{ static_cast<O>(rd.y), static_cast<O>(td.y), static_cast<O>(fd.y) },
			{ static_cast<O>(rd.z), static_cast<O>(td.z), static_cast<O>(fd.z) }
		}; //rotMatrix after scaleMatrix
		copy2DArray<O, 3, 3>(
			pm,
			*mat_out
		);
	}
	
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void globalToLocalSpace(O (*mat_out)[3][3]) const {
		auto const rd = rightDir();
		auto const td = topDir();
		auto const fd = forwardDir();
		
		O const pm[3][3] = {
			{ static_cast<O>(rd.x), static_cast<O>(rd.y), static_cast<O>(rd.z) },
			{ static_cast<O>(td.x), static_cast<O>(td.y), static_cast<O>(td.z) },
			{ static_cast<O>(fd.x), static_cast<O>(fd.y), static_cast<O>(fd.z) }
		};
		copy2DArray<O, 3, 3>(
			pm,
			*mat_out
		);
	}
	
	template<typename O, typename = std::enable_if_t<std::is_convertible<double, O>::value>>
	void projectionMatrix(O (*mat_out)[4][4]) const {		
		auto const htF{ tan(fov / 2.0) };
		O const pm[4][4] = {
			{ static_cast<O>(1/htF*aspectRatio), static_cast<O>(0.0), static_cast<O>(0), static_cast<O>(0.0) },
			{ static_cast<O>(0.0), static_cast<O>(1/htF), static_cast<O>(0.0), static_cast<O>(0.0) },
			{ static_cast<O>(0.0), static_cast<O>(0.0), static_cast<O>( far / (far - near)), static_cast<O>(-(far * near) / (far - near)) },
			{ static_cast<O>(0.0), static_cast<O>(0.0), static_cast<O>( 1.0), static_cast<O>(0.0) }
		};
	
		copy2DArray<O, 4, 4>(
			pm,
			*mat_out
		);
	}
};