#pragma once



#include <filesystem>
#include <ostream>



namespace gpsxre
{

class SystemISO
{
public:
	SystemISO(const std::filesystem::path &track_path);

	void operator()(std::ostream &os) const;

private:
	std::filesystem::path _trackPath;
	uint64_t _trackSize;
};

}
