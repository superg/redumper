module;
#include <cstdint>
#include <filesystem>
#include <format>
#include <ostream>
#include <ranges>
#include "system.hh"

export module systems.hfs;

import filesystem.apm;
import filesystem.hfs;
import readers.data_reader;
import utils.strings;
import utils.endian;



namespace gpsxre
{

export class SystemHFS : public System
{
public:
    std::string getName() override
    {
        return "HFS";
    }

    Type getType() override
    {
        return Type::DISK;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &) const override
    {
        auto hfs_partitions = apm::Browser::getPartitions(data_reader) | std::views::filter([](const apm::PartitionMapEntry &v) { return v.partition_type == apm::PARTITION_TYPE_APPLE_HFS; });
        for(const auto &partition_map_entry : hfs_partitions)
        {
            const uint32_t mdb_location = endian_swap(partition_map_entry.partition_start_sector) + hfs::MDB_OFFSET;
            hfs::MasterDirectoryBlock mdb;
            if(data_reader->read((uint8_t *)&mdb, data_reader->sectorsBase() + mdb_location, 1) != 1)
                continue;

            if(endian_swap(mdb.volume_signature) == hfs::VOLUME_SIGNATURE_HFS)
            {
                std::string volume_identifier = mac_roman_to_utf8(from_pascal_string(mdb.volume_name));
                os << std::format("  volume identifier: {}", volume_identifier) << std::endl;
            }
        }
    }
};

}
