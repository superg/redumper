module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>

export module systems.iso;

import filesystem.iso9660_browser;
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

	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &track_path) const override;
};


void SystemISO::printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &track_path) const
{
	ISO9660::PrimaryVolumeDescriptor pvd;
	if(ISO9660::Browser::findDescriptor((ISO9660::VolumeDescriptor &)pvd, sector_reader, ISO9660::VolumeDescriptorType::PRIMARY))
	{
		auto volume_identifier = trim(pvd.volume_identifier);
		if(!volume_identifier.empty())
			os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
		os << "  PVD:" << std::endl;
		os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
	}
}

}
