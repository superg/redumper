module;
#include <cstdint>
#include <array>

export module filesystem.hfs:defs;

import utils.strings;



export namespace gpsxre::hfs
{

// Based on https://developer.apple.com/library/archive/documentation/mac/pdf/Files/File_Manager.pdf

constexpr uint16_t VOLUME_SIGNATURE_HFS = 0x4244;
constexpr uint16_t VOLUME_SIGNATURE_MFS = 0xD2D7;
constexpr uint32_t MDB_OFFSET = 2;

#pragma pack(push, 1)
struct ExtentDescriptor
{
    uint16_t extent_start_allocation_block;
    uint16_t extent_num_allocation_blocks;
};


struct ExtentDataRecord
{
    std::array<ExtentDescriptor, 3> extents;
};


struct MasterDirectoryBlock
{
    uint16_t volume_signature;
    uint32_t volume_creation_date;
    uint32_t volume_last_modification_date;
    uint16_t volume_attributes;
    uint16_t root_directory_file_count;
    uint16_t volume_bitmap_start_block;
    uint16_t allocation_search_start_block;
    uint16_t total_allocation_blocks;
    uint32_t allocation_block_size;
    uint32_t default_clump_size;
    uint16_t first_allocation_block;
    uint32_t next_catalog_node_id;
    uint16_t free_allocation_blocks;
    pascal_string<27> volume_name;
    uint32_t last_backup_date;
    uint16_t volume_backup_sequence_number;
    uint32_t volume_write_count;
    uint32_t extents_overflow_clump_size;
    uint32_t catalog_file_clump_size;
    uint16_t root_directory_directory_count;
    uint32_t total_file_count;
    uint32_t total_directory_count;
    std::array<uint32_t, 8> finder_information;
    uint16_t volume_cache_size;
    uint16_t volume_bitmap_cache_size;
    uint16_t common_volume_cache_size;
    uint32_t extents_overflow_file_size;
    ExtentDataRecord extents_overflow_extent_record;
    uint32_t catalog_file_size;
    ExtentDataRecord catalog_file_extent_record;
    uint8_t reserved[350];
};
#pragma pack(pop)

}
