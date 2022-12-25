#pragma once



#include <filesystem>
#include <functional>
#include <list>
#include <ostream>



namespace gpsxre
{

class System
{
public:
	static System &get();

	typedef std::function<void(std::ostream &os)> Callback;
	std::list<Callback> getSystems(const std::filesystem::path &track_path) const;

private:
	static System _system;
};

}
