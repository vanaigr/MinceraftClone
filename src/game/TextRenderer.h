#pragma once
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
	#include "GLEW/glew.h"
#pragma clang diagnostic pop

#include"font/Font.h"
#include"Vector.h"

#include<string_view>
#include<cstddef>
#include<stdint.h>
#include<memory>

struct TextRenderer {
	enum class VAlign {
		top, center, bottom
	};	
	enum class HAlign {
		left, center, right
	};
	
	struct Cursor {
		uint32_t color;
		int count;
	};

	GLuint fontVB;
	GLuint fontVA;
public:
	TextRenderer();

	void draw(
		std::string_view const text, TextRenderer::HAlign const textHAlign,
		
		Cursor const *const beginCursor, Cursor const *const endCursor,
		vec2f const origin, TextRenderer::VAlign const originVAlign, TextRenderer::HAlign const originHAlign, 
		
		float const lineCount, //in one screen
		Font const &font, vec2i const windowSize, GLuint const program
	);
};