module;
#include <cstring>
#include <memory>
#include <optional>
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

    static std::shared_ptr<udf::FileEntry> rootDirectory(DataReader *data_reader)
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

            // Search for the logical volume descriptor. This contains the extent of the file set descriptor.
            auto lvd_offset = findDescriptorOffset(data_reader, avdp.value().main_vds, udf::TagIdentifier::LOGICAL);
            if(!lvd_offset.has_value())
            {
                return {};
            }
            std::vector<uint8_t> lvd_data(FORM1_DATA_SIZE);
            data_reader->read(lvd_data.data(), lvd_offset.value(), 1);
            struct udf::LogicalVolumeDescriptor logical_volume_descriptor = (udf::LogicalVolumeDescriptor &)lvd_data[0];

            auto const &file_set_descriptor_extent = (udf::long_ad &)logical_volume_descriptor.logical_volume_contents_use;

            // If we have the partition the file set descriptor is found in, parse it to find the root directory ICB extent.
            if(partition_descriptor.partition_number == file_set_descriptor_extent.extent_location.partition_reference_number)
            {
                std::vector<uint8_t> fsd_data(FORM1_DATA_SIZE);
                data_reader->read(fsd_data.data(), partition_descriptor.partition_starting_location + file_set_descriptor_extent.extent_location.logical_block_number, 1);
                auto const &file_set_descriptor = (udf::FileSetDescriptor &)fsd_data[0];

                auto const &root_directory_icb_extent = file_set_descriptor.root_directory_icb;

                // If we have the partition the root directory is in, read and return it.
                if(partition_descriptor.partition_number == root_directory_icb_extent.extent_location.partition_reference_number)
                {
                    std::vector<uint8_t> root_directory_data(FORM1_DATA_SIZE);
                    data_reader->read(root_directory_data.data(), partition_descriptor.partition_starting_location + root_directory_icb_extent.extent_location.logical_block_number, 1);

                    auto root_directory_file_entry = std::shared_ptr<udf::FileEntry>(new udf::FileEntry());
                    std::memcpy(root_directory_file_entry.get(), root_directory_data.data(), sizeof(udf::FileEntry));
                    return root_directory_file_entry;
                }
            }
        }

        return {};
    }
};

}
