module;

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
#include <vector>
#include "system.hh"

export module systems.dc;

import filesystem.iso9660;
import readers.sector_reader;
import utils.hex_bin;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemDC : public System
{
public:
    std::string getName() override
    {
        return "DC";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
    {
        auto system_area = iso9660::Browser::readSystemArea(sector_reader);
        if(system_area.size() < _ROM_HEADER_OFFSET + sizeof(ROMHeader) || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
            return;

        std::vector<uint8_t> rom_header_data(system_area.data() + _ROM_HEADER_OFFSET, system_area.data() + _ROM_HEADER_OFFSET + sizeof(ROMHeader));

        auto rom_header = (ROMHeader *)rom_header_data.data();

        std::string date = extractDate(std::string(rom_header->date, sizeof(rom_header->date)));
        if(!date.empty())
            os << std::format("  build date: {}", date) << std::endl;

        std::string version = extractVersion(std::string(rom_header->version, sizeof(rom_header->version)));
        if(!version.empty())
            os << std::format("  version: {}", version) << std::endl;

        std::string serial(rom_header->serial, sizeof(rom_header->serial));
        erase_all_inplace(serial, ' ');
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
    static constexpr std::string_view _SYSTEM_MAGIC = "SEGA SEGAKATANA";
    static constexpr uint32_t _ROM_HEADER_OFFSET = 0x000;
    static constexpr uint32_t _DATE_SYMBOLS = 8;
    static constexpr uint32_t _YEAR_SYMBOLS = 4;
    static constexpr uint32_t _MONTH_SYMBOLS = 2;
    static constexpr uint32_t _DAY_SYMBOLS = 2;
    static constexpr uint32_t _VERSION_SYMBOLS = 6;
    static const std::map<char, std::string> _REGIONS;

    struct ROMHeader
    {
        char system_name[16];
        char maker_id[16];
        char device_info[16];

        char regions[3];
        char reserved1[5];
        char peripherals[8];

        char serial[10];
        char version[6];

        char date[8];
        char reserved2[8];

        char exe_filename[16];
        char disc_manufacturer[16];

        char title[128];
    };


    std::string extractDate(std::string date) const
    {
        if(date.length() != _DATE_SYMBOLS)
            return "";

        auto year_index = str_to_uint64(std::string(date, 0, _YEAR_SYMBOLS));
        if(!year_index || !number_is_year(*year_index))
            return "";

        auto month_index = str_to_uint64(std::string(date, 4, _MONTH_SYMBOLS));
        if(!month_index || !number_is_month(*month_index))
            return "";

        auto day_index = str_to_uint64(std::string(date, 6, _DAY_SYMBOLS));
        if(!day_index || !number_is_day(*day_index))
            return "";

        date.insert(4, "-");
        date.insert(7, "-");

        return date;
    }


    std::string extractVersion(std::string version) const
    {
        if(version.length() != _VERSION_SYMBOLS)
            return "";

        if(version[0] != 'V')
            return "";

        version.erase(0, 1);
        erase_all_inplace(version, ' ');

        for(uint32_t i = 0; i < version.length(); ++i)
        {
            char ch = version[i];
            if(!std::isdigit(ch) && ch != '.')
                return "";
        }

        return version;
    }
};


const std::map<char, std::string> SystemDC::_REGIONS = {
    { 'J', "Japan"  },
    { 'U', "USA"    },
    { 'E', "Europe" }
};

}
