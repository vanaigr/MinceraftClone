#include<fstream>
#include<iostream>

//https://libgdx.com/wiki/tools/hiero
int main() {
	std::ifstream fnt{"./assets/font.fnt"};
	std::ofstream out{"./assets/font.txt"};
	
	if(!fnt.is_open()) {
		std::cout << "ERROR: could not open ./assets/font.fnt for reading\n";
		exit(-1);
	}	
	if(!out.is_open()) {
		std::cout << "ERROR: could not open ./assets/font.txt for wrighting\n";
		exit(-1);
	}
	
	bool nFirst{};
	while(true) {
		int cur;
		while(cur = fnt.get(), !(cur == '=' || cur == -1));
		if(cur == -1) break;
		if(nFirst) out << ',';
		else nFirst = true;
		while(cur = fnt.get(), !(cur == ' ' || cur == -1 || cur == '\r'  || cur == '\n')) out << char(cur);
		if(cur == -1) break;
	}
	return 0;
}