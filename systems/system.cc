#include "cdrom.hh"
#include "iso.hh"
#include "system.hh"



namespace gpsxre
{

System System::_system;


System &System::get()
{
	return _system;
}


std::list<std::function<void(std::ostream &os)>> System::getSystems(const std::filesystem::path &track_path) const
{
	std::list<std::function<void(std::ostream &os)>> systems;

	systems.emplace_back(SystemCDROM(track_path));
	systems.emplace_back(SystemISO(track_path));

	return systems;
}

}
