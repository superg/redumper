module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <ostream>
#include <string_view>

export module systems.mcd;

import filesystem.image_browser;
import utils.hex_bin;



namespace gpsxre
{

export class SystemMCD
{
public:
	SystemMCD(const std::filesystem::path &track_path)
		: _trackPath(track_path)
		, _trackSize(std::filesystem::file_size(track_path))
	{
		;
	}


	void operator()(std::ostream &os) const;

private:
	static constexpr std::string_view _SYSTEM_MAGIC = "SEGADISCSYSTEM";
	static constexpr uint32_t _HEADER_OFFSET = 0x100;
	static constexpr uint32_t _HEADER_SIZE = 0x100;

	std::filesystem::path _trackPath;
	uint64_t _trackSize;
};


void SystemMCD::operator()(std::ostream &os) const
{
	if(!ImageBrowser::IsDataTrack(_trackPath))
		return;

	ImageBrowser browser(_trackPath, 0, _trackSize, false);

	auto system_area = browser.getSystemArea();
	if(system_area.size() < _SYSTEM_MAGIC.size() || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
		return;

	os << std::format("MCD [{}]:", _trackPath.filename().string()) << std::endl;
	os << "  header:" << std::endl;
	os << std::format("{}", hexdump(system_area.data(), _HEADER_OFFSET, _HEADER_SIZE));
}

}
