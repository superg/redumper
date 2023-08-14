module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>

export module systems.iso;

import filesystem.image_browser;
import systems.system;
import utils.hex_bin;
import utils.strings;



namespace gpsxre
{

export class SystemISO : public System
{
public:
	std::string getName() override
	{
		return "ISO9660";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os, const std::filesystem::path &track_path) const override;
};


void SystemISO::printInfo(std::ostream &os, const std::filesystem::path &track_path) const
{
	if(ImageBrowser::IsDataTrack(track_path))
	{
		ImageBrowser browser(track_path, 0, std::filesystem::file_size(track_path), false);
		
		auto pvd = browser.GetPVD();

		auto volume_identifier = trim(pvd.primary.volume_identifier);
		if(!volume_identifier.empty())
			os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
		os << "  PVD:" << std::endl;
		os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
	}
}

}
