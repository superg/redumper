module;
#include <algorithm>
#include <cstdint>
#include <optional>

export module filesystem.high_sierra:browser;

import :defs;
import readers.data_reader;



export namespace gpsxre::high_sierra
{

class Browser
{
public:
    static std::optional<VolumeDescriptor> findDescriptor(DataReader *data_reader, VolumeDescriptorType type)
    {
        VolumeDescriptor descriptor;

        for(uint32_t s = SYSTEM_AREA_SIZE; data_reader->read((uint8_t *)&descriptor, data_reader->sectorsBase() + s, 1) == 1; ++s)
        {
            if(!std::equal(descriptor.vd.standard_identifier, descriptor.vd.standard_identifier + sizeof(descriptor.vd.standard_identifier), STANDARD_IDENTIFIER))
                break;

            if(descriptor.vd.type == type)
                return descriptor;
            else if(descriptor.vd.type == VolumeDescriptorType::SET_TERMINATOR)
                break;
        }

        return std::nullopt;
    }
};

}
