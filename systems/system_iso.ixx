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

export class SystemISO : public SystemT<SystemISO>
{
public:
	SystemISO(const std::filesystem::path &track_path)
		: _trackPath(track_path)
		, _trackSize(std::filesystem::file_size(track_path))
	{
		;
	}

	static std::string name()
	{
		return "ISO9660";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os) const override;

private:
	std::filesystem::path _trackPath;
	uint64_t _trackSize;
};


void SystemISO::printInfo(std::ostream &os) const
{
	if(ImageBrowser::IsDataTrack(_trackPath))
	{
		ImageBrowser browser(_trackPath, 0, _trackSize, false);
		
		auto pvd = browser.GetPVD();

		auto volume_identifier = trim(pvd.primary.volume_identifier);
		if(!volume_identifier.empty())
			os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
		os << "  PVD:" << std::endl;
		os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
	}
}

}
