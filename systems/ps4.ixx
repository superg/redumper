module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps4;

import filesystem.iso9660;
import readers.sector_reader;
import systems.ps3;



namespace gpsxre
{

export class SystemPS4 : public SystemPS3
{
public:
    std::string getName() override
    {
        return "PS4";
    }


    Type getType() override
    {
        return Type::ISO;
    }


    void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, sector_reader, iso9660::VolumeDescriptorType::PRIMARY))
            return;
        auto root_directory = iso9660::Browser::rootDirectory(sector_reader, pvd);

        auto sfo_entry = root_directory->subEntry("bd/param.sfo");
        if(!sfo_entry)
            return;

        const uint32_t payload_skip = 0x800;
        auto sfo_raw = sfo_entry->read();
        if(sfo_raw.size() < payload_skip)
            return;

        auto param_sfo = parseSFO(std::span<uint8_t>(sfo_raw.begin() + payload_skip, sfo_raw.end()));

        if(auto it = param_sfo.find("VERSION"); it != param_sfo.end())
            os << std::format("  version: {}", it->second) << std::endl;

        if(auto it = param_sfo.find("TITLE_ID"); it != param_sfo.end())
        {
            auto serial = it->second;
            serial.insert(4, "-");
            os << std::format("  serial: {}", serial) << std::endl;
        }
    }
};

}
