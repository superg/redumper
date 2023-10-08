module;
#include <functional>
#include <list>
#include <memory>

export module systems.systems;

import systems.cdrom;
import systems.iso;
import systems.mcd;
import systems.psx;
import systems.ps2;
import systems.securom;
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
		
		systems.push_back([](){ return std::make_unique<SystemCDROM>(); });
		systems.push_back([](){ return std::make_unique<SystemSecuROM>(); });
		systems.push_back([](){ return std::make_unique<SystemISO>(); });
		systems.push_back([](){ return std::make_unique<SystemMCD>(); });
		systems.push_back([](){ return std::make_unique<SystemPSX>(); });
		systems.push_back([](){ return std::make_unique<SystemPS2>(); });
		systems.push_back([](){ return std::make_unique<SystemSS>(); });

		return systems;
	}
};

}
