module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include "system.hh"

export module systems.iso;

import filesystem.iso9660;
import readers.data_reader;
import utils.hex_bin;
import utils.strings;



namespace gpsxre
{

export class SystemISO : public System
{
public:
    std::string getName() override
    {
        return "ISO9660";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
        {
            auto volume_identifier = iso9660::identifier_to_string(pvd.volume_identifier);
            if(!volume_identifier.empty())
                os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
            os << "  PVD:" << std::endl;
            os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
        }
    }
};

}
