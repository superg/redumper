module;
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

export module filesystem.udf:browser;

import :defs;

import cd.cdrom;
import readers.data_reader;

export namespace gpsxre::udf
{

class Browser
{
public:
    const static std::optional<udf::AnchorVolumeDescriptorPointer> findAnchorVolumeDescriptorPointer(DataReader *data_reader)
    {
        std::vector<uint8_t> data(FORM1_DATA_SIZE);
        data_reader->read(data.data(), AVDP_PRIMARY_LBA, 1);

        auto const &avdp = (udf::AnchorVolumeDescriptorPointer &)data[0];

        if(avdp.descriptor_tag.tag_identifier == udf::TagIdentifier::ANCHOR_POINTER)
        {
            return avdp;
        }
        else
        {
            return {};
        }
    }

    const static std::optional<uint32_t> findDescriptorOffset(DataReader *data_reader, ExtentDescriptor main_vds, TagIdentifier type)
    {
        for(uint32_t lba = main_vds.location; lba <= main_vds.location + ((main_vds.length - 1) / FORM1_DATA_SIZE); ++lba)
        {
            std::vector<uint8_t> data(FORM1_DATA_SIZE);
            data_reader->read(data.data(), lba, 1);

            auto const &tag = (udf::DescriptorTag &)data[0];

            if(tag.tag_identifier == type)
            {
                return lba;
            }
            else if(tag.tag_identifier == udf::TagIdentifier::TERMINATING)
            {
                break;
            }
        }

        return {};
    }

    static std::variant<std::shared_ptr<udf::FileEntry>, std::shared_ptr<udf::ExtendedFileEntry>> rootDirectory(DataReader *data_reader)
    {
        // Try and find the AVDP so we can look through the volume descriptor sequence.
        auto const avdp = findAnchorVolumeDescriptorPointer(data_reader);
        if(avdp.has_value())
        {
            // Search for a partition in the volume descriptor sequence. This is probably partition 0.
            auto partition_offset = findDescriptorOffset(data_reader, avdp.value().main_vds, udf::TagIdentifier::PARTITION);
            if(!partition_offset.has_value())
            {
                return {};
            }
            std::vector<uint8_t> partition_data(FORM1_DATA_SIZE);

            data_reader->read(partition_data.data(), partition_offset.value(), 1);
            struct udf::PartitionDescriptor partition_descriptor = (udf::PartitionDescriptor &)partition_data[0];

            // For the moment, we're just assuming that partitions are inserted into this in order.
            std::vector<uint32_t> partition_starting_locations;
            partition_starting_locations.push_back(partition_descriptor.partition_starting_location);

            // Search for the logical volume descriptor. This contains the extent of the file set descriptor.
            auto lvd_offset = findDescriptorOffset(data_reader, avdp.value().main_vds, udf::TagIdentifier::LOGICAL);
            if(!lvd_offset.has_value())
            {
                return {};
            }
            std::vector<uint8_t> lvd_data(FORM1_DATA_SIZE);
            data_reader->read(lvd_data.data(), lvd_offset.value(), 1);
            struct udf::LogicalVolumeDescriptor logical_volume_descriptor = (udf::LogicalVolumeDescriptor &)lvd_data[0];

            // Parse the partition maps in the logical volume descriptor to find the metadata partition map, if it's present.
            auto partition_map_offset = sizeof(udf::LogicalVolumeDescriptor);
            for(int i = 0; i < logical_volume_descriptor.number_of_partition_maps; i++)
            {
                struct udf::partition_map_header partition_map_header = (udf::partition_map_header &)lvd_data[partition_map_offset];

                if(2 == partition_map_header.partition_map_type)
                {
                    struct udf::partition_map_type_2_header partition_map_type_2_header = (udf::partition_map_type_2_header &)lvd_data[partition_map_offset];

                    // While we know the identifier is only 23 bytes long, if we don't wrap it in a bounded string view the comparison will continue reading after it until it finds a null terminator.
                    if(std::string_view(partition_map_type_2_header.partition_type_identifier.identifier, 23) == PARTITION_TYPE_ID_METADATA)
                    {
                        // If we've found the metadata partition, parse it as an extended file entry and use the position of the first allocation descriptor to determine the start of partition 1.
                        struct udf::metadata_partition_map metadata_partition_map = (udf::metadata_partition_map &)lvd_data[partition_map_offset];

                        std::vector<uint8_t> metadata_file_data(FORM1_DATA_SIZE);
                        data_reader->read(metadata_file_data.data(), partition_starting_locations[metadata_partition_map.partition_number] + metadata_partition_map.metadata_file_location, 1);
                        struct udf::ExtendedFileEntry metadata_file = (udf::ExtendedFileEntry &)metadata_file_data[0];

                        // This isn't particularly well explained in the spec, so this behavior comes from libudfread.
                        struct udf::short_ad metadata_file_ad = (udf::short_ad &)metadata_file_data[sizeof(udf::ExtendedFileEntry) + metadata_file.length_of_extended_attributes];
                        partition_starting_locations.push_back(partition_starting_locations[0] + metadata_file_ad.extent_position);
                    }
                }
                partition_map_offset += partition_map_header.partition_map_length;
            }

            auto const &file_set_descriptor_extent = (udf::long_ad &)logical_volume_descriptor.logical_volume_contents_use;

            // If we have the partition the file set descriptor is found in, parse it to find the root directory ICB extent.
            if(partition_starting_locations.size() - 1 >= file_set_descriptor_extent.extent_location.partition_reference_number)
            {
                std::vector<uint8_t> fsd_data(FORM1_DATA_SIZE);
                data_reader->read(fsd_data.data(),
                    partition_starting_locations[file_set_descriptor_extent.extent_location.partition_reference_number] + file_set_descriptor_extent.extent_location.logical_block_number, 1);
                auto const &file_set_descriptor = (udf::FileSetDescriptor &)fsd_data[0];

                auto const &root_directory_icb_extent = file_set_descriptor.root_directory_icb;

                // If we have the partition the root directory is in, read and return it.
                if(partition_starting_locations.size() - 1 >= root_directory_icb_extent.extent_location.partition_reference_number)
                {
                    std::vector<uint8_t> root_directory_data(FORM1_DATA_SIZE);
                    data_reader->read(root_directory_data.data(),
                        partition_starting_locations[root_directory_icb_extent.extent_location.partition_reference_number] + root_directory_icb_extent.extent_location.logical_block_number, 1);

                    auto const &tag = (udf::DescriptorTag &)root_directory_data[0];
                    if(tag.tag_identifier == udf::TagIdentifier::FILE_ENTRY)
                    {
                        auto root_directory_file_entry = std::shared_ptr<udf::FileEntry>(new udf::FileEntry());
                        std::memcpy(root_directory_file_entry.get(), root_directory_data.data(), sizeof(udf::FileEntry));
                        return root_directory_file_entry;
                    }
                    else if(tag.tag_identifier == udf::TagIdentifier::EXTENDED_FILE_ENTRY)
                    {
                        auto root_directory_file_entry = std::shared_ptr<udf::ExtendedFileEntry>(new udf::ExtendedFileEntry());
                        std::memcpy(root_directory_file_entry.get(), root_directory_data.data(), sizeof(udf::ExtendedFileEntry));

                        return root_directory_file_entry;
                    }
                }
            }
        }

        return {};
    }
};

}
