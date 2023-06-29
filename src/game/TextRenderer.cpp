#include"TextRenderer.h"

#include<vector>
#include<cstring>

#pragma pack(push, 1)
struct TextParams {
	vec2<GLfloat> startPos;
	vec2<GLfloat> endPos;
	vec2<GLfloat> startUV;
	vec2<GLfloat> endUV;
	uint32_t color;
};
#pragma pack(pop)

TextRenderer::TextRenderer() {
	glGenBuffers(1, &fontVB);
	
	glGenVertexArrays(1, &fontVA);
	glBindVertexArray(fontVA);
		glBindBuffer(GL_ARRAY_BUFFER, fontVB);
		
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glEnableVertexAttribArray(4);
	
		glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, sizeof(TextParams), (void*)offsetof(TextParams, startPos));
		glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, sizeof(TextParams), (void*)offsetof(TextParams, endPos));
		glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, sizeof(TextParams), (void*)offsetof(TextParams, startUV));
		glVertexAttribPointer (3, 2, GL_FLOAT, GL_FALSE, sizeof(TextParams), (void*)offsetof(TextParams, endUV));
		glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT   , sizeof(TextParams), (void*)offsetof(TextParams, color));
	
		glVertexAttribDivisorARB(0, 1);
		glVertexAttribDivisorARB(1, 1);
		glVertexAttribDivisorARB(2, 1);
		glVertexAttribDivisorARB(3, 1);
		glVertexAttribDivisorARB(4, 1);
	glBindVertexArray(0);
}

void TextRenderer::draw(
	std::string_view const text, TextRenderer::HAlign const textHAlign,
	
	Cursor const *const beginCursor, Cursor const *const endCursor,
	vec2f const origin, TextRenderer::VAlign const originVAlign, TextRenderer::HAlign const originHAlign,
	
	float const lineCount, 
	Font const &font, vec2i const windowSize, GLuint const program
) {
	assert(beginCursor != endCursor);
	
	vec2i const fontSize{ font.width, font.height };
	auto const aspectRatio{ float(windowSize.y) / windowSize.x };
	auto const scale{ 1.0 / lineCount };
	
	auto const startPoint{ origin / vec2f(windowSize) * font.lineHeight * lineCount };
	
	//calculate text bounding box and chars count
	static std::vector<float> linesWidth{};
	linesWidth.clear();
	float currentLineWidth{};
	vec2f dimensions{};
	size_t count{};
	
	for(size_t i{};; i++) {
		auto const ch{ text[i] };
		auto const end{ i == text.size() };
		if(ch == '\n' || end) {
			linesWidth.push_back(currentLineWidth);
			dimensions = vec2f(std::max(dimensions.x, currentLineWidth), dimensions.y + font.lineHeight);
			currentLineWidth = 0;
			if(end) break;
		}
		else {
			auto const fc{ font.fontChars[int(ch)] };			
			currentLineWidth += fc.xAdvance * aspectRatio;
			if(ch != ' ') count++;
		}
	}
	
	auto const alignment{ [&]() {
		float x, y;
		
		if(originVAlign == VAlign::top) y = -font.base + font.lineHeight;
		else if(originVAlign == VAlign::center) y = -dimensions.y / 2.0f;
		else y = -dimensions.y;	
		
		if(originHAlign == HAlign::left) x = 0;
		else if(originHAlign == HAlign::center) x = -dimensions.x / 2.0f;
		else x = -dimensions.x;
		
		return vec2f{x,y};
	}() };
	
	size_t const size{ count * sizeof(TextParams) };
	
	
	glBindBuffer(GL_ARRAY_BUFFER, fontVB);
	{ //resize buffer
		GLint bufferSize;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
		assert(bufferSize >= 0);
		if(size > size_t(bufferSize)) glBufferData(GL_ARRAY_BUFFER, sizeof(TextParams) * size, NULL, GL_DYNAMIC_DRAW);
	}
	
	//fill the buffer
	do {
		void *const data{ glMapBufferRange(
			GL_ARRAY_BUFFER,
			0,
			size,
			GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT
		) };
		assert(data != nullptr);
		
		auto currentData{ 0 }; //index of curent TextParams in data
		
		auto currentPoint{ startPoint };
		auto currentLine{ 0 };
		auto const *currentCursor{ beginCursor };
		auto cursorCount{ 0 };
		
		for(auto const ch : text) {
			if(cursorCount >= currentCursor->count) {
				decltype(auto) nextCursor{ currentCursor + 1 };
				if(nextCursor != endCursor) {
					cursorCount = 0;
					currentCursor = nextCursor;
				}
			}
			
			if(ch == '\n') {
				currentPoint = { startPoint.x, currentPoint.y + font.lineHeight };
				currentLine++;
			}
			else {
				auto const fc{ font.fontChars[int(ch)] };
				
				auto const lineXOffset{ [&]() {
					if(textHAlign == HAlign::left) return 0.0f;
					if(textHAlign == HAlign::center) return  (dimensions.x - linesWidth[currentLine]) / 2.0f;
					else return dimensions.x - linesWidth[currentLine];
				}() };
				
				auto const charOffset{ vec2f(fc.xOffset, fc.yOffset) + alignment + vec2f(lineXOffset, 0) };
				if(ch != ' ') {
					TextParams const params{
						//pos
						(currentPoint + vec2f( 0            , fc.height) + charOffset) / font.lineHeight * scale,
						(currentPoint + vec2f(fc.width * aspectRatio, 0) + charOffset) / font.lineHeight * scale, 
						//uv
						vec2f(fc.x, fc.y+fc.height) / vec2f(fontSize),
						vec2f(fc.x+fc.width, fc.y) / vec2f(fontSize),
						//color
						currentCursor->color
					};
					
					memcpy((char*)data + currentData*sizeof(TextParams), &params, sizeof(TextParams));
					currentData++;
				}
				
				currentPoint.x += fc.xAdvance * aspectRatio;
			}		
			cursorCount++; //note: spaces and newlines also count
		}
	} while(!glUnmapBuffer(GL_ARRAY_BUFFER));
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//draw the text
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glUseProgram(program);
	
	glBindVertexArray(fontVA);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
	glBindVertexArray(0);
	
	glDisable(GL_BLEND);
}
