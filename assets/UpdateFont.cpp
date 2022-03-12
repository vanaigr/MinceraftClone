#include<fstream>
#include<iostream>

//https://libgdx.com/wiki/tools/hiero
int main() {
	std::ifstream fnt{".\\font.fnt", std::ios::binary};
	std::ofstream out{".\\font.txt"};
	
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