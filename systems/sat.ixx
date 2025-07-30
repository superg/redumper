module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ostream>
#include <set>
#include <string_view>
#include <utility>
#include <vector>
#include "system.hh"

export module systems.sat;

import filesystem.iso9660;
import readers.data_reader;
import utils.hex_bin;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemSAT : public System
{
public:
    std::string getName() override
    {
        return "SS";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool) const override
    {
        auto system_area = iso9660::Browser::readSystemArea(data_reader);
        if(system_area.size() < _SYSTEM_MAGIC.size() || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
            return;

        std::vector<uint8_t> rom_header_data(system_area.data() + _ROM_HEADER_OFFSET, system_area.data() + _ROM_HEADER_OFFSET + sizeof(ROMHeader));

        auto rom_header = (ROMHeader *)rom_header_data.data();

        std::string date = extractDate(std::string(rom_header->date, sizeof(rom_header->date)));
        if(!date.empty())
            os << std::format("  build date: {}", date) << std::endl;

        auto [version, serial] = extractSerialVersion(std::string(rom_header->serialversion, sizeof(rom_header->serialversion)));
        if(!version.empty())
            os << std::format("  version: {}", version) << std::endl;
        if(!serial.empty())
            os << std::format("  serial: {}", serial) << std::endl;

        std::string regions(rom_header->regions, sizeof(rom_header->regions));
        erase_all_inplace(regions, ' ');

        std::set<std::string> unique_regions;
        for(auto r : regions)
        {
            auto it = _REGIONS.find(r);
            if(it != _REGIONS.end())
                unique_regions.insert(it->second);
        }

        if(!unique_regions.empty())
        {
            os << (unique_regions.size() == 1 ? "  region: " : "  regions: ");
            bool comma = false;
            for(auto r : unique_regions)
            {
                os << (comma ? ", " : "") << r;
                comma = true;
            }
            os << std::endl;
        }

        os << "  header:" << std::endl;
        os << std::format("{}", hexdump(system_area.data(), _ROM_HEADER_OFFSET, sizeof(ROMHeader)));
    }

private:
    static constexpr std::string_view _SYSTEM_MAGIC = "SEGA SEGASATURN";
    static constexpr uint32_t _ROM_HEADER_OFFSET = 0;
    static constexpr uint32_t _YEAR_SYMBOLS = 4;
    static constexpr uint32_t _MONTH_SYMBOLS = 2;
    static constexpr uint32_t _DAY_SYMBOLS = 2;
    static const std::map<char, std::string> _REGIONS;

    struct ROMHeader
    {
        char system_name[16];
        char maker_id[16];

        char serialversion[16];

        char date[8];
        char device_info[8];

        char regions[10];
        char reserved_spaces[6];

        char peripherals[16];

        char title[112];

        char reserved[16];

        char initial_program_size[4];
        char reserved2[4];
        char master_stack_address[4];
        char slave_stack_address[4];

        char first_read_address[4];
        char first_read_size[4];
        char reserved3[8];
    };


    std::string extractDate(std::string date) const
    {
        auto year_index = str_to_uint64(std::string(date, 0, _YEAR_SYMBOLS));
        if(!year_index || !number_is_year(*year_index))
            return "";
        auto month_index = str_to_uint64(std::string(date, _YEAR_SYMBOLS, _MONTH_SYMBOLS));
        if(!month_index || !number_is_month(*month_index))
            return "";
        auto day_index = str_to_uint64(std::string(date, _YEAR_SYMBOLS + _MONTH_SYMBOLS, _DAY_SYMBOLS));
        if(!day_index || !number_is_day(*day_index))
            return "";
        date.insert(4, "-");
        date.insert(7, "-");
        return date;
    }


    std::pair<std::string, std::string> extractSerialVersion(std::string serialversion) const
    {
        auto p = serialversion.rfind('V');
        std::string serial = serialversion.substr(0, p);
        trim_inplace(serial);

        std::string version;
        if(p != std::string::npos)
        {
            auto v = serialversion.substr(p + 1);
            erase_all_inplace(v, ' ');

            if(std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(c) || c == '.'; }))
                version = v;
        }

        return std::pair(version, serial);
    }
};


const std::map<char, std::string> SystemSAT::_REGIONS = {
    { 'J', "Japan"         },
    { 'T', "Asia NTSC"     },
    { 'U', "USA"           },
    { 'B', "Brazil"        },
    { 'K', "South Korea"   },
    { 'A', "Asia PAL"      },
    { 'E', "Europe"        },
    { 'L', "Latin America" }
};

}
