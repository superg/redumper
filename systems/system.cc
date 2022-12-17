#include "iso.hh"



namespace gpsxre
{

std::list<System::Static::Creator> System::Static::_creators;
System::Static System::_static;

System::Static::Static()
{
	_creators.push_back([](const std::filesystem::path &file_path) -> std::shared_ptr<System> { return std::make_shared<SystemISO>(file_path); });
}


std::list<std::shared_ptr<System>> System::getSystems(const std::filesystem::path &file_path)
{
	std::list<std::shared_ptr<System>> systems;

	for(auto c : _static._creators)
	{
		auto system = c(file_path);
		if(system->isValid())
			systems.push_back(system);
	}

	return systems;
}

}
