module;
#include <functional>
#include <list>
#include <memory>

export module systems.systems;

import systems.cdrom;
import systems.iso;
import systems.mcd;
import systems.psx;
import systems.ss;
import systems.system;



namespace gpsxre
{

export class Systems
{
public:
	using Creator = std::function<std::unique_ptr<System>()>;

	Systems() = delete;

	static std::list<Creator> get()
	{
		std::list<Creator> systems;
		
		systems.push_back([](){ return std::unique_ptr<System>(new SystemCDROM()); });
		systems.push_back([](){ return std::unique_ptr<System>(new SystemISO()); });
		systems.push_back([](){ return std::unique_ptr<System>(new SystemMCD()); });
		systems.push_back([](){ return std::unique_ptr<System>(new SystemPSX()); });
		systems.push_back([](){ return std::unique_ptr<System>(new SystemSS()); });

		return systems;
	}
};

}
