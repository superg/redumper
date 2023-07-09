module;
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "throw_line.hh"

export module dump;

import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import drive;
import options;
import cd.toc;
import utils.logger;



namespace gpsxre
{

export enum class DiscType
{
	NONE,
	CD,
	DVD,
	BLURAY
};


export struct Context
{
	DiscType disc_type;
	std::unique_ptr<SPTD> sptd;
	DriveConfig drive_config;

	struct Dump
	{
		bool refine;
		bool interrupted;
	};
	std::unique_ptr<Dump> dump;
};


export enum class DumpMode
{
	DUMP,
	VERIFY,
	REFINE
};


export enum class DumpStatus
{
	SUCCESS,
	ERRORS,
	INTERRUPTED
};


export constexpr uint32_t SLOW_SECTOR_TIMEOUT = 5;
#if 1
export constexpr int32_t LBA_START = -45150; //MSVC internal compiler error: MSF_to_LBA(MSF_LEADIN_START); // -45150
#else
// easier debugging, LBA starts with 0, plextor lead-in and asus cache are disabled
export constexpr int32_t LBA_START = 0;
// GS2v3   13922 .. 17080-17090
// GS2_1.1 12762 .. 17075
// GS2_5.5 12859 .. 17130-17140
// GS2_1.2 12739 .. 16930-16940
// SC DISC  8546 .. 17100-17125
// SC BOX  10547 .. 16940-16950
// CB4 6407-7114 ..  9200- 9220
// GS GCD   9162 .. 17000-17010  // F05 0004
// XPLO FM  7770 .. 10700-10704
//static constexpr int32_t LBA_START = MSF_to_LBA(MSF_LEADIN_START);
#endif


export enum class State : uint8_t
{
	ERROR_SKIP, // must be first to support random offset file writes
	ERROR_C2,
	SUCCESS_C2_OFF,
	SUCCESS_SCSI_OFF,
	SUCCESS
};


export void image_check_overwrite(std::filesystem::path state_path, const Options &options)
{
	if(!options.overwrite && std::filesystem::exists(state_path))
		throw_line("dump already exists (image name: {})", options.image_name);
}


export void print_toc(const TOC &toc)
{
	std::stringstream ss;
	toc.print(ss);

	std::string line;
	while(std::getline(ss, line))
		LOG("{}", line);
}


export int32_t sample_offset_a2r(uint32_t absolute)
{
	return absolute + (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export uint32_t sample_offset_r2a(int32_t relative)
{
	return relative - (LBA_START * CD_DATA_SIZE_SAMPLES);
}

}
