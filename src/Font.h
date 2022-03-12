#include<iostream>

struct FontChar {
	//char id=0       x=0    y=0    width=0    height=0    xoffset=-1   yoffset=0    xadvance=1    page=0    chnl=0 
	
	float x;
	float y;
	
	float width;
	float height;
	
	float xOffset;
	float yOffset;
	
	float xAdvance;
	
	FontChar() = default;
	
	FontChar(float const (&d)[7]) : 
		x{d[0]},
		y{d[1]},
		width{d[2]},
		height{d[3]},
		xOffset{d[4]},
		yOffset{d[5]},
		xAdvance{d[6]}
	{}
	
	friend std::ostream &operator<<(std::ostream &o, FontChar const &i) {
		return o << '('
			<< i.x << ';'<< i.y << ';'
			<< i.width << ';'<< i.height << ';' 
			<< i.xOffset << ';' << i.yOffset << ';'
			<< i.xAdvance << ')';
	}
};

struct Font {
private: FontChar fontChars[255]; 
		 float lineHeight_;
		 float base_;
public:
	Font(char const *path);
	Font(Font const &) = default;
	Font & operator=(Font const &)= default;
	~Font() = default;
	
	FontChar const &operator[](int index) const;
	
	float lineHeight() const {
		return lineHeight_;
	}
	float base() const {
		return base_;
	}
};