#include"Font.h"
#include<fstream>
#include<iostream>
#include<cassert>

Font::Font(char const *path) {
	//std::memset(&fontChars[0], 0, sizeof(fontChars));
	std::ifstream info{ path };
	
	auto const parseField = [](std::ifstream &i) -> int {
		int field = 0;
		
		int cur;
		bool minus = false;
		while(cur = i.get(), cur != -1 && cur != ',') 
			if(cur == '-') minus = true;
			else field = field * 10 + (cur - '0');
		
		if(minus) field *= -1;
		
		return field;
	};
	
	for(int i = 0; i < 15; i++) parseField(info);
	lineHeight_ = parseField(info);
	base_ = parseField(info);
	auto const width = float(parseField(info));
	auto const height = float(parseField(info));
	
	lineHeight_ /= height;
	base_ /= height;
	
	int const pages{ parseField(info) };
	if(pages != 1) {
		std::cerr << "Error: pages count " << pages << " is != 1\n";
		assert(false);
	}
	parseField(info); //packed
	parseField(info); //id
	parseField(info); //file
	parseField(info); //count
	
	while(true) {
		int fieldIndex = 0;
		float fields[7];
		int index;
		
		while(true) {
			if(fieldIndex == 8) {
				FontChar const it{ fields };
				if(index < 0 || index > 256) {
					std::cerr << "font char " << index << " is not in ASCII set\n";
					exit(-1);
				}
				fontChars[index] = it;
				//std::cout << fontChars[index] << '\n';
				fieldIndex = 0;
				
				parseField(info); //page
				parseField(info); //chnl
			}
			
			if(fieldIndex == 0) index = parseField(info);
			else fields[fieldIndex-1] = float(parseField(info)) / (fieldIndex % 2 == 1 ? width : height);
		
			if(info.peek() == -1) return;
			fieldIndex ++;
		}
	}
}

FontChar const &Font::operator[](int index) const {
	return fontChars[index];
}

