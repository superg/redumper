module;
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <queue>
#include <string>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660;

export import :defs;

import cd.cd;
//import cd.cdrom;
//import cd.scrambler;
//import filesystem.iso9660;
import readers.sector_reader;
//import utils.endian;
//import utils.misc;
//import utils.strings;



//#define DIRECTORY_RECORD_WORKAROUNDS



namespace gpsxre
{

export namespace iso9660
{

class Browser
{
public:
	Browser(SectorReader *sector_reader)
	{
		if(!findDescriptor((VolumeDescriptor &)_pvd, sector_reader, VolumeDescriptorType::PRIMARY))
			throw_line("PVD not found");
	}

/*
	std::shared_ptr<Entry> RootDirectory()
	{
		return std::shared_ptr<Entry>(new Entry(*this, std::string(""), 1, _pvd.root_directory_record));
	}
*/

	static bool findDescriptor(VolumeDescriptor &descriptor, SectorReader *sector_reader, VolumeDescriptorType type)
	{
		bool found = false;

		for(uint32_t s = SYSTEM_AREA_SIZE; sector_reader->read((uint8_t *)&descriptor, s, 1); ++s)
		{
			if(memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER, sizeof(descriptor.standard_identifier)) &&
				memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER_CDI, sizeof(descriptor.standard_identifier)))
				break;

			if(descriptor.type == type)
			{
				found = true;
				break;
			}
			else if(descriptor.type == VolumeDescriptorType::SET_TERMINATOR)
				break;
		}

		return found;
	}

private:
	PrimaryVolumeDescriptor _pvd;
};

}

}
