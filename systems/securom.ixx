module;
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <vector>
#include "system.hh"
#include "throw_line.hh"



export module systems.securom;

import cd.cd;
import cd.cdrom;
import cd.subcode;
import crc.crc16_gsm;
import dump;
import readers.sector_reader;
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
        // SecuROM data track has one sector with flipped mode
        static constexpr uint32_t flip_offset = 4;
        auto sectors_count = sector_reader->sectorsCount();
        if(sectors_count >= flip_offset)
        {
            Sector sector[2];
            sector_reader->read((uint8_t *)&sector, sectors_count - flip_offset, countof(sector));

            if(sector[0].header.mode == sector[1].header.mode)
                return;
        }

        std::filesystem::path sub_path = track_extract_basename(track_path.string()) + ".subcode";
        if(!std::filesystem::exists(sub_path))
            return;

        std::vector<ChannelP> subp;
        std::vector<ChannelQ> subq;
        subcode_load_subpq(subp, subq, sub_path);
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
                // scheme 1, 2
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

                // scheme 2, 3, 4
                if((crc_current ^ 0x8001) == crc_fixed)
                {
                    if(!sequence_active || sequence_counter == 8)
                    {
                        candidates.push_back(lba);
                        sequence_counter = 0;
                    }
                }
                // scheme 1
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

        uint32_t scheme = 0;
        if(candidates.size() == 216)
            scheme = 1;
        else if(candidates.size() == 90)
            scheme = 2;
        else if(candidates.size() == 98 || candidates.size() == 99 && candidates.front() == -1)
            scheme = 3;
        else if(candidates.size() == 10 || candidates.size() == 11 && candidates.front() == -1)
            scheme = 4;

        if(!candidates.empty())
        {
            os << std::format("  scheme: {}", scheme ? std::to_string(scheme) : "unknown") << std::endl;
            for(auto const &c : candidates)
                redump_print_subq(os, c, subq[c - LBA_START]);
        }
    }
};

}
