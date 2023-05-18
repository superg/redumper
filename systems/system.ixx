module;
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <ostream>

export module systems.system;

import systems.cdrom;
import systems.iso;
import systems.mcd;
import systems.psx;



namespace gpsxre
{

export class System
{
public:
	static System &get()
	{
		return _system;
	}

	typedef std::function<void(std::ostream &os)> Callback;
	std::list<Callback> getSystems(const std::filesystem::path &track_path) const
	{
		std::list<std::function<void(std::ostream &os)>> systems;

		systems.emplace_back(SystemCDROM(track_path));
		systems.emplace_back(SystemISO(track_path));
		systems.emplace_back(SystemMCD(track_path));
		systems.emplace_back(SystemPSX(track_path));

		return systems;
	}

private:
	static System _system;
};

System System::_system;

}
