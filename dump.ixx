module;
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "throw_line.hh"

export module dump;

import cd.cd;
import cd.subcode;
import cd.toc;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.file_io;
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
	std::shared_ptr<SPTD> sptd;
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


export std::vector<ChannelQ> load_subq(const std::filesystem::path &sub_path)
{
	std::vector<ChannelQ> subq(std::filesystem::file_size(sub_path) / CD_SUBCODE_SIZE);

	std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
	if(!fs.is_open())
		throw_line("unable to open file ({})", sub_path.filename().string());

	std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
	for(uint32_t lba_index = 0; lba_index < subq.size(); ++lba_index)
	{
		read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba_index, 1, 0, 0);
		subcode_extract_channel((uint8_t *)&subq[lba_index], sub_buffer.data(), Subchannel::Q);
	}

	return subq;
}


export std::ostream &redump_print_subq(std::ostream &os, int32_t lba, const ChannelQ &Q)
{
	MSF msf = LBA_to_MSF(lba);
	os << std::format("MSF: {:02}:{:02}:{:02} Q-Data: {:X}{:X}{:02X}{:02X} {:02X}:{:02X}:{:02X} {:02X} {:02X}:{:02X}:{:02X} {:04X}",
					  msf.m, msf.s, msf.f, (uint8_t)Q.control, (uint8_t)Q.adr, Q.mode1.tno, Q.mode1.point_index, Q.mode1.msf.m, Q.mode1.msf.s, Q.mode1.msf.f, Q.mode1.zero, Q.mode1.a_msf.m, Q.mode1.a_msf.s, Q.mode1.a_msf.f, endian_swap<uint16_t>(Q.crc)) << std::endl;
	
	return os;
}

}
