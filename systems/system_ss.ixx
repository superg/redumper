module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <ostream>
#include <string_view>

export module systems.ss;

import filesystem.image_browser;
import systems.system;
import utils.hex_bin;



namespace gpsxre
{

export class SystemSS : public System
{
public:
	std::string getName() override
	{
		return "SS";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &track_path) const override;

private:
	static constexpr std::string_view _SYSTEM_MAGIC = "SEGA SEGASATURN";
	static constexpr uint32_t _HEADER_OFFSET = 0;
	static constexpr uint32_t _HEADER_SIZE = 0x100;
};


void SystemSS::printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &track_path) const
{
	if(!ImageBrowser::IsDataTrack(track_path))
		return;

	ImageBrowser browser(track_path, 0, std::filesystem::file_size(track_path), false);

	auto system_area = browser.getSystemArea();
	if(system_area.size() < _SYSTEM_MAGIC.size() || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
		return;

	os << "  header:" << std::endl;
	os << std::format("{}", hexdump(system_area.data(), _HEADER_OFFSET, _HEADER_SIZE));
}

}
