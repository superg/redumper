#pragma once



#include <filesystem>
#include <ostream>



namespace gpsxre
{

class SystemCDROM
{
public:
	SystemCDROM(const std::filesystem::path &track_path);

	void operator()(std::ostream &os) const;

private:
	std::filesystem::path _trackPath;
};

}
