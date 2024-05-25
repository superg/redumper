module;

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <string_view>
#include "system.hh"

export module systems.sat;

import filesystem.iso9660;
import readers.sector_reader;
import utils.hex_bin;



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

    void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
    {
        auto system_area = iso9660::Browser::readSystemArea(sector_reader);
        if(system_area.size() < _SYSTEM_MAGIC.size() || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
            return;

        os << "  header:" << std::endl;
        os << std::format("{}", hexdump(system_area.data(), _HEADER_OFFSET, _HEADER_SIZE));
    }

private:
    static constexpr std::string_view _SYSTEM_MAGIC = "SEGA SEGASATURN";
    static constexpr uint32_t _HEADER_OFFSET = 0;
    static constexpr uint32_t _HEADER_SIZE = 0x100;
};

}
