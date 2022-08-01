#include"Font.h"
#include<fstream>
#include<iostream>
#include<cassert>

void loadFont(Font &font, char const *const path) {
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
	
	for(int i = 0; i < 9; i++) parseField(info);
	font.paddingT = parseField(info);
	font.paddingR = parseField(info);
	font.paddingB = parseField(info);
	font.paddingL = parseField(info);
	for(int i = 0; i < 2; i++) parseField(info);
	
	
	int paddings[] = { 1, 1, -1, -1, 0, 0, 0 };
	
	font.lineHeight = parseField(info);
	font.base = parseField(info);
	font.width = parseField(info);
	font.height = parseField(info);
	
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
		int fields[7];
		int index;
		
		while(true) {
			if(fieldIndex == 8) {
				parseField(info); //page
				parseField(info); //chnl
				
				font.fontChars[index] = FontChar::fromArray(fields);
				
				fieldIndex = 0;
				continue;
			}
			
			if(fieldIndex == 0) {
				index = parseField(info);
				assert(index >= 0 && index < 256);
			}
			else fields[fieldIndex-1] = parseField(info) + paddings[fieldIndex-1];
		
			if(info.peek() == -1) return;
			fieldIndex ++;
		}
	}
}

