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

export class SystemSS : public SystemT<SystemSS>
{
public:
	SystemSS(const std::filesystem::path &track_path)
		: _trackPath(track_path)
		, _trackSize(std::filesystem::file_size(track_path))
	{
		;
	}

	static std::string name()
	{
		return "SS";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os) const override;

private:
	static constexpr std::string_view _SYSTEM_MAGIC = "SEGA SEGASATURN";
	static constexpr uint32_t _HEADER_OFFSET = 0;
	static constexpr uint32_t _HEADER_SIZE = 0x100;

	std::filesystem::path _trackPath;
	uint64_t _trackSize;
};


void SystemSS::printInfo(std::ostream &os) const
{
	if(!ImageBrowser::IsDataTrack(_trackPath))
		return;

	ImageBrowser browser(_trackPath, 0, _trackSize, false);

	auto system_area = browser.getSystemArea();
	if(system_area.size() < _SYSTEM_MAGIC.size() || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
		return;

	os << "  header:" << std::endl;
	os << std::format("{}", hexdump(system_area.data(), _HEADER_OFFSET, _HEADER_SIZE));
}

}
