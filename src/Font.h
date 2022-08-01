#include<iostream>
#include<cassert>

struct FontChar {
	static FontChar fromArray(int const (&d)[7]) {
		return { d[0], d[1], d[2], d[3], d[4], d[5], d[6] };
	}
public:
	int x;
	int y;
	
	int width;
	int height;
	
	int xOffset;
	int yOffset;
	
	int xAdvance;

	friend std::ostream &operator<<(std::ostream &o, FontChar const &i) {
		return o << '('
			<< i.x << ';'<< i.y << ';'
			<< i.width << ';'<< i.height << ';' 
			<< i.xOffset << ';' << i.yOffset << ';'
			<< i.xAdvance << ')';
	}
};

struct Font {
	FontChar fontChars[256]; 	
	int paddingT;
	int paddingR;
	int paddingB;
	int paddingL;
	
	int width;
	int height;
	
	int lineHeight;
	int base;
};

void loadFont(Font &font, char const *const path);