module;
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
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
import utils.misc;



namespace gpsxre
{

export struct Context
{
	GET_CONFIGURATION_FeatureCode_ProfileList current_profile;
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


export constexpr int32_t LBA_START = -45150; //MSVC internal compiler error: MSF_to_LBA(MSF_LEADIN_START); // -45150


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


export bool subcode_correct_subq(ChannelQ *subq, uint32_t sectors_count)
{
	uint32_t mcn = sectors_count;
	std::map<uint8_t, uint32_t> isrc;
	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	bool invalid_subq = true;
	uint8_t tno = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!subq[lba_index].isValid())
			continue;

		invalid_subq = false;

		if(subq[lba_index].adr == 1)
			tno = subq[lba_index].mode1.tno;
		else if(subq[lba_index].adr == 2 && mcn == sectors_count)
			mcn = lba_index;
		else if(subq[lba_index].adr == 3 && tno && isrc.find(tno) == isrc.end())
			isrc[tno] = lba_index;
	}

	if(invalid_subq)
		return false;

	uint32_t q_prev = sectors_count;
	uint32_t q_next = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!memcmp(&subq[lba_index], &q_empty, sizeof(q_empty)))
			continue;

		// treat unexpected MSF as invalid (SecuROM)
		if(subq[lba_index].isValid(lba_index + LBA_START))
		{
			if(subq[lba_index].adr == 1)
			{
				if(subq[lba_index].mode1.tno)
					q_prev = lba_index;
				else
					q_prev = sectors_count;
			}
		}
		else
		{
			// find next valid Q
			if(lba_index >= q_next && q_next != sectors_count)
			{
				q_next = lba_index + 1;
				for(; q_next < sectors_count; ++q_next)
					if(subq[q_next].isValid(q_next + LBA_START))
					{
						if(subq[q_next].adr == 1)
						{
							if(!subq[q_next].mode1.tno)
								q_next = 0;

							break;
						}
					}
			}

			std::vector<ChannelQ> candidates;
			if(q_prev < lba_index)
			{
				// mode 1
				candidates.emplace_back(subq[q_prev].generateMode1(lba_index - q_prev));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subq[q_prev].generateMode23(subq[mcn], lba_index - q_prev));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_prev].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subq[q_prev].generateMode23(subq[it->second], lba_index - q_prev));
				}
			}

			if(q_next > lba_index && q_next != sectors_count)
			{
				// mode 1
				candidates.emplace_back(subq[q_next].generateMode1(lba_index - q_next));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subq[q_next].generateMode23(subq[mcn], lba_index - q_next));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_next].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subq[q_next].generateMode23(subq[it->second], lba_index - q_next));
				}
			}

			if(!candidates.empty())
			{
				uint32_t c = 0;
				for(uint32_t j = 0; j < (uint32_t)candidates.size(); ++j)
					if(bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[j], sizeof(ChannelQ) / sizeof(uint32_t)) < bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[c], sizeof(ChannelQ) / sizeof(uint32_t)))
						c = j;

				subq[lba_index] = candidates[c];
			}
		}
	}

	return true;
}


export std::ostream &redump_print_subq(std::ostream &os, int32_t lba, const ChannelQ &Q)
{
	MSF msf = LBA_to_MSF(lba);
	os << std::format("MSF: {:02}:{:02}:{:02} Q-Data: {:X}{:X}{:02X}{:02X} {:02X}:{:02X}:{:02X} {:02X} {:02X}:{:02X}:{:02X} {:04X}",
					  msf.m, msf.s, msf.f, (uint8_t)Q.control, (uint8_t)Q.adr, Q.mode1.tno, Q.mode1.point_index, Q.mode1.msf.m, Q.mode1.msf.s, Q.mode1.msf.f, Q.mode1.zero, Q.mode1.a_msf.m, Q.mode1.a_msf.s, Q.mode1.a_msf.f, endian_swap<uint16_t>(Q.crc)) << std::endl;
	
	return os;
}


export bool profile_is_cd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
	return profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_R
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW;
}


export bool profile_is_dvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
	return profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R;
}


export bool profile_is_bluray(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
	return profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW;
}


export bool profile_is_hddvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
	return profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL
		|| profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL;
}

}
