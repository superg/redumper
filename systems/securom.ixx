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

		std::vector<int32_t> candidates;
		uint32_t sequence_counter = 0;
		bool sequence_active = false;

		for(uint32_t lba_index = 0; lba_index < subq_fixed.size(); ++lba_index)
		{
			int32_t lba = lba_index + LBA_START;

			if(subq[lba_index].isValid())
			{
				// version 1, 2
				if(!subq[lba_index].isValid(lba))
				{
					if(!sequence_active)
					{
						candidates.clear();
						sequence_active = true;
					}

					if(candidates.empty() || candidates.back() + 1 != lba)
						sequence_counter = 0;

					candidates.push_back(lba);
					++sequence_counter;
				}
			}
			else if(subq_fixed[lba_index].isValid())
			{
				uint16_t crc_current = endian_swap(subq[lba_index].crc);
				uint16_t crc_expected = CRC16_GSM().update(subq[lba_index].raw, sizeof(subq[lba_index].raw)).final();
				uint16_t crc_fixed = endian_swap(subq_fixed[lba_index].crc);

				// version 2, 3, 4
				if((crc_current ^ 0x8001) == crc_fixed)
				{
					if(!sequence_active || sequence_counter == 8)
					{
						candidates.push_back(lba);
						sequence_counter = 0;
					}
				}
				// version 1
				else if((crc_current ^ 0x0080) == crc_expected)
				{
					if(sequence_active && sequence_counter == 8)
					{
						candidates.push_back(lba);
						sequence_counter = 0;
					}
				}
			}
		}

		uint32_t version = 0;
		if(candidates.size() == 216)
			version = 1;
		else if(candidates.size() == 90)
			version = 2;
		else if(candidates.size() == 98 || candidates.size() == 99 && candidates.front() == -1)
			version = 3;
		else if(candidates.size() == 10 || candidates.size() == 11 && candidates.front() == -1)
			version = 4;

		if(version)
		{
			os << std::format("  version: {}", version) << std::endl;
			for(auto const &c : candidates)
				redump_print_subq(os, c, subq[c - LBA_START]);
		}
	}
};

}
