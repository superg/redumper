module;
#include <cstdint>
#include <ostream>
#include <vector>

export module filesystem.apm:browser;

import :defs;
import readers.data_reader;
import utils.strings;
import utils.endian;


export namespace gpsxre::apm
{

class Browser
{
public:
    static std::vector<PartitionMapEntry> getPartitions(DataReader *data_reader)
    {
        std::vector<PartitionMapEntry> partitions;

        DriveDescriptor drive_descriptor;
        if(data_reader->read((uint8_t *)&drive_descriptor, data_reader->sectorsBase(), 1) == 1)
        {
            if(to_string_view(drive_descriptor.signature) == DRIVE_DESCRIPTOR_SIGNATURE)
            {
                PartitionMapEntry partition_map_entry;
                uint32_t entry_index = 0;
                constexpr uint32_t MAX_PARTITION_ENTRIES = 512;
                do
                {
                    if(data_reader->read((uint8_t *)&partition_map_entry, data_reader->sectorsBase() + PARTITION_MAP_OFFSET + entry_index, 1) != 1)
                        break;

                    if(to_string_view(partition_map_entry.signature) == PARTITION_MAP_ENTRY_SIGNATURE)
                        partitions.push_back(partition_map_entry);
                } while(++entry_index < std::min(endian_swap(partition_map_entry.entry_count), MAX_PARTITION_ENTRIES));
            }
        }

        return partitions;
    }
};

}
