module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>

export module systems.iso;

import filesystem.image_browser;
import utils.hex_bin;



namespace gpsxre
{

export class SystemISO
{
public:
	SystemISO(const std::filesystem::path &track_path)
		: _trackPath(track_path)
		, _trackSize(std::filesystem::file_size(track_path))
	{
		;
	}


	void operator()(std::ostream &os) const;

private:
	std::filesystem::path _trackPath;
	uint64_t _trackSize;
};


void SystemISO::operator()(std::ostream &os) const
{
	if(ImageBrowser::IsDataTrack(_trackPath))
	{
		ImageBrowser browser(_trackPath, 0, _trackSize, false);
		
		os << std::format("ISO9660 [{}]:", _trackPath.filename().string()) << std::endl;

		auto pvd = browser.GetPVD();
		os << "  PVD:" << std::endl;
		os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
	}
}

}
