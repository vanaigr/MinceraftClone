#include"Config.h"
#include"Misc.h"

#include<variant>
#include<fstream>
#include<string>


//field type
struct NoValue{}; //for default constructor of variant
using Field = std::variant<NoValue, int, double, bool, std::string>;
enum class FT { NO_VALUE, INT, REAL, BOOL, STRING };

//cofig fields
enum class CF {
	viewDistance,
	loadChunks,
	saveChunks,
	playerCameraFov,
	mouseSensitivityX,
	mouseSensitivityY,
	chunkUpdatesPerFrame,
	lockFramerate,
	worldName,
	groupSizeX,
	groupSizeY
};
static constexpr int fieldCount = 13;

//Config field names
static constexpr char const *CFN[] = {
	"viewDistance",
	"loadChunks",
	"saveChunks",
	"playerCameraFovDeg",
	"mouseSensitivity_x",
	"mouseSensitivity_y",
	"chunkUpdatesPerFrame",
	"lockFramerate",
	"worldName",
	"groupSizeX",
	"groupSizeY"
};

//Config field types
static constexpr FT CFT[] = {
	FT::INT ,
	FT::BOOL,
	FT::BOOL,
	FT::REAL,
	FT::REAL,
	FT::REAL,
	FT::INT ,
	FT::BOOL,
	FT::STRING,
	FT::INT,
	FT::INT
};


//https://stackoverflow.com/a/52303687/18704284
template <typename> struct tag { }; // <== this one IS literal

template <typename T, typename V>
struct get_index;

template <typename T, typename... Ts> 
struct get_index<T, std::variant<Ts...>>
    : std::integral_constant<size_t, std::variant<tag<Ts>...>(tag<T>()).index()>
{ };

//note: this same function already exists in Misc.h
//https://stackoverflow.com/a/8357462/18704284
template <typename E>
static constexpr typename std::underlying_type<E>::type indexOf(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}


void parseConfigFromFile(Config &dst) {	
	std::ifstream cfgFile{ "game.cfg" };
	
	if(!cfgFile.is_open()) {
		std::cout << "ERROR: could not open config file\n";
		return;
	}
	
	std::string name{};
	std::string value{};
	
	while(cfgFile >> name) {
		//find field index by name
		int field = 0;
		while(true) {
			if(field >= fieldCount) {
				std::cout << "ERROR: no property named `" << name << "`\n";
				return;
			}
			if(name == CFN[field]) break;
			field++;
		}
		
		//parse field value
		Field parsedValue{};
		try{
			if(CFT[field] != FT::NO_VALUE && !(cfgFile >> value)) {
				std::cout << "ERROR: no value for `" << name << "`\n";
				return;
			}
			#pragma clang diagnostic push
			#pragma clang diagnostic ignored "-Winvalid-token-paste"
			#define result(FUNCTION) [&](){ size_t count; decltype(auto) res{ std::##FUNCTION##(value, &count) }; if(count != value.size()) throw int(); else return res; }()
				
			switch(CFT[field]) {
				break; case FT::NO_VALUE : parsedValue.emplace<indexOf(FT::NO_VALUE)>(NoValue{});
				break; case FT::INT      : parsedValue.emplace<indexOf(FT::INT     )>(result(stoi));
				break; case FT::REAL     : parsedValue.emplace<indexOf(FT::REAL    )>(result(stod));
				break; case FT::BOOL     : parsedValue.emplace<indexOf(FT::BOOL    )>([&](){ 
					auto const res{result(stoi)}; 
					if(res == 1) return true; 
					else if(res == 0) return false;
					else throw int();
				}() );
				break; case FT::STRING   : parsedValue.emplace<indexOf(FT::STRING  )>(value       );
			}
			
			#undef result
			#pragma clang diagnostic pop
		} 
		catch(...) {
			std::cout << "ERROR: cannot parse value `" << value << "` for property `" << name << "`\n";
			return;
		}
		
		//set field value
		try{
			#define result(FIELD) std::get<indexOf(CFT[indexOf(FIELD)])>(parsedValue)
			switch(static_cast<CF>(field)) {
				break; case CF::viewDistance         : dst.viewDistance         = result(CF::viewDistance);
				break; case CF::loadChunks           : dst.loadChunks           = result(CF::loadChunks);
				break; case CF::saveChunks           : dst.saveChunks           = result(CF::saveChunks);
				break; case CF::playerCameraFov      : dst.playerCameraFOV      = result(CF::playerCameraFov) / 180.0 * misc::pi;
				break; case CF::mouseSensitivityX    : dst.mouseSensitivity.x   = result(CF::mouseSensitivityX);
				break; case CF::mouseSensitivityY    : dst.mouseSensitivity.y   = result(CF::mouseSensitivityY);
				break; case CF::chunkUpdatesPerFrame : dst.chunkUpdatesPerFrame = result(CF::chunkUpdatesPerFrame);
				break; case CF::lockFramerate        : dst.lockFramerate        = result(CF::lockFramerate);
				break; case CF::worldName            : dst.worldName            = result(CF::worldName);
				break; case CF::groupSizeX           : dst.groupSize.x          = result(CF::groupSizeX);
				break; case CF::groupSizeY           : dst.groupSize.y          = result(CF::groupSizeY);
			}
			#undef result
		}
		catch(...) {
			std::cout << "ERROR: cannot use value `" << value << "` for property `" << name << "`\n";
			return;
		}                  
	}
}