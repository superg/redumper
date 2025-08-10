module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include "system.hh"

export module systems.high_sierra;

import filesystem.high_sierra;
import filesystem.iso9660;
import readers.data_reader;
import utils.hex_bin;
import utils.strings;



namespace gpsxre
{

export class SystemHighSierra : public System
{
public:
    std::string getName() override
    {
        return "High Sierra";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool) const override
    {
        auto vd = high_sierra::Browser::findDescriptor(data_reader, high_sierra::VolumeDescriptorType::STANDARD_FS);
        if(vd)
        {
            auto &sfsvd = (high_sierra::StandardFileStructureVolumeDescriptor &)*vd;
            auto volume_identifier = iso9660::identifier_to_string(sfsvd.volume_identifier);
            if(!volume_identifier.empty())
                os << std::format("  volume identifier: {}", volume_identifier) << std::endl;

            // start 13 bytes before volume_creation_date_time to keep it the same as iso9660
            auto offset = offsetof(high_sierra::StandardFileStructureVolumeDescriptor, volume_creation_date_time) - 13;

            os << "  SFSVD:" << std::endl;
            os << std::format("{}", hexdump((uint8_t *)&vd, offset, 96));
        }
    }
};

}
