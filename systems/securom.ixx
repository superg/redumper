module;
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <vector>
#include "throw_line.hh"

export module systems.securom;

import cd.cd;
import cd.subcode;
import crc.crc16_gsm;
import dump;
import readers.sector_reader;
import systems.system;
import utils.endian;
import utils.file_io;
import utils.misc;



namespace gpsxre
{

export class SystemSecuROM : public System
{
public:
	std::string getName() override
	{
		return "SecuROM";
	}


	Type getType() override
	{
		return Type::RAW_DATA;
	}


	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &track_path) const override
	{
		std::filesystem::path sub_path = track_extract_basename(track_path.string()) + ".subcode";
		if(!std::filesystem::exists(sub_path))
			return;

		std::vector<ChannelQ> subq = load_subq(sub_path);
		std::vector<ChannelQ> subq_fixed = subq;
		if(!subcode_correct_subq(subq_fixed.data(), subq_fixed.size()))
			return;

		std::vector<int32_t> candidates, candidates_8001;

		for(uint32_t lba_index = 0; lba_index < subq_fixed.size(); ++lba_index)
		{
			if(!subq_fixed[lba_index].isValid() || subq[lba_index].isValid())
				continue;

			int32_t lba = lba_index + LBA_START;

			uint16_t crc_current = endian_swap(subq[lba_index].crc);
			uint16_t crc_expected = CRC16_GSM().update(subq[lba_index].raw, sizeof(subq[lba_index].raw)).final();
			uint16_t crc_fixed = endian_swap(subq_fixed[lba_index].crc);

			if((crc_current ^ 0x0080) == crc_expected)
				candidates.push_back(lba);
			else if((crc_current ^ 0x8001) == crc_fixed)
				candidates_8001.push_back(lba);
		}

		uint32_t version = 0;

		if(candidates_8001.size() == 10 || candidates_8001.size() == 11 && candidates_8001.front() == -1)
		{
			candidates.swap(candidates_8001);
			version = 4;
		}

		if(version)
		{
			os << std::format("  version: {}", version) << std::endl;
			for(auto const &c : candidates)
				redump_print_subq(os, c, subq[c - LBA_START]);
		}
	}
};

}
