#include"image/Read.h"
#include"image/SaveBMP.h"
#include"font/Font.h"

#include<cassert>
#include<stdint.h>
#include<math.h>

int main() {
	Image image;
	ImageLoad("./assets/font.bmp", &image);
	
	auto id = image.data.get();
	int const iW = image.sizeX;
	int const iH = image.sizeY;
	int const size = iW * iH;
	
	int16_t *info = new int16_t[size]();
	
	float *info2 = new float[size]();
	float minD = iW;
	float maxD = -iW;
	
	
	unsigned char *data = new unsigned char[size*3]();
	auto const dataAt = [&](auto x, auto y) -> auto& { return data[(x + (iH-1 - y) * iW) * 3]; };
	
	
	Font f{};
	loadFont(f, "./assets/font.txt");
	
	auto const imgAt = [&](auto x, auto y) -> int { return int(static_cast<unsigned char>(id[(x + (iH-1 - y) * iW) * 3])) - 128; };
	auto const infoAt = [&](auto x, auto y) -> auto& { return info[x + y * iW]; };
	auto const info2At = [&](auto x, auto y) -> auto& { return info2[x + y * iW]; };
	
	for(int i = 0; i < 256; i++) {
		auto const cha{ f.fontChars[i] };
		
		for(int y{}; y < cha.height; y++) {
			for(int x{}; x < cha.width; x++) {
				bool inside{ imgAt(cha.x + x, cha.y + y) >= 0 };
				int minDist = iW;
				
				for(int x2{}; x2 < cha.width; x2++) {
					auto dist = abs(x - x2);
					if(dist >= minDist) continue;
					auto otherInside = imgAt(cha.x + x2, cha.y + y) >= 0;
					
					if(inside != otherInside) {
						minDist = dist;
					}
				}
				
				if(minDist == iW) {
					infoAt(cha.x + x, cha.y + y) = 0;
				}
				else {
					auto dist = ((inside ? -1 : 1) * minDist);
					infoAt(cha.x + x, cha.y + y) = dist;
				}
			}
		}
		
		for(int y{}; y < cha.height; y++) {
			for(int x{}; x < cha.width; x++) {
				bool inside{ imgAt(cha.x + x, cha.y + y) >= 0 };
				float minDistSq = iW*iW + iH*iH;
				
				for(int y2{}; y2 < cha.height; y2++) {
					auto distY = abs(y - y2);
					if(distY*distY >= minDistSq) continue;
					
					auto tryOther = [&](){
						auto distX = infoAt(cha.x + x, cha.y + y2);
						if(distX == 0) return true;
						
						auto distSq = distX*distX + distY*distY;
						if(distSq >= minDistSq) return true;
						
						auto otherInside = distX < 0;
						if(inside == otherInside) {
							minDistSq = distSq;
							return false;
						}
						return true;
					}();
					
					if(tryOther) {
						auto otherInside = imgAt(cha.x + x, cha.y + y2) >= 0;
						if(inside != otherInside) {
							minDistSq = distY*distY;
						}
					}
				}
				
				if(minDistSq == iW*iW + iH*iH) {
					info2At(cha.x + x, cha.y + y) = 0.0f;
				}
				else {
					auto distSq = (inside ? -1 : 1) * minDistSq;
					
					info2At(cha.x + x, cha.y + y) = distSq;
					
					minD = std::min(distSq, minD);
					maxD = std::max(distSq, maxD);
				}
			}
		}
	}
	
	minD = std::copysign(std::sqrt(std::abs(minD)), minD);
	maxD = std::copysign(std::sqrt(std::abs(maxD)), maxD);
	float diff = std::max(abs(maxD), abs(minD));
	
	std::cout << maxD << ' ' << minD << '\n';

	for(int y{}; y < iH; y++) {
		for(int x{}; x < iW; x++) {
			auto dat = info2At(x, y);
			if(dat == 0.0f) dataAt(x, y) = 128;
			else {
				auto d = std::copysign(std::sqrt(std::abs(dat)), dat);
				int value = d / diff * 128 + 128;
				dataAt(x, y) = std::min(std::max<int>(value, 0), 255);
			}
		}
	}

	generateBitmapImage(data, image.sizeY, image.sizeX, "assets/sdfFont.bmp"); 
	
	return 0;
}