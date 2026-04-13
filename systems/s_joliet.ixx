module;
#include <filesystem>
#include <format>
#include <ostream>
#include "system.hh"

export module systems.joliet;

import filesystem.iso9660;
import filesystem.joliet;
import readers.data_reader;



namespace gpsxre
{

export class SystemJoliet : public System
{
public:
    std::string getName() override
    {
        return "Joliet";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool) const override
    {
        iso9660::SupplementaryVolumeDescriptor svd;
        if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)svd, data_reader, iso9660::VolumeDescriptorType::SUPPLEMENTARY))
        {
            if(joliet::ESCAPE_SEQUENCES.contains(svd.escape_sequences))
            {
                auto volume_identifier = joliet::identifier_to_string((joliet::VolumeIdentifier &)svd.volume_identifier);
                if(!volume_identifier.empty())
                    os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
            }
        }
    }
};

}
