module;
#include <cstdint>
#include <set>
#include <string_view>

export module filesystem.udf:defs;



export namespace gpsxre::udf
{

constexpr uint32_t AVDP_PRIMARY_LBA = 256;
constexpr std::string_view DESCRIPTOR_ID_BEA = "BEA01";
constexpr std::string_view DESCRIPTOR_ID_BOOT2 = "BOOT2";
constexpr std::string_view DESCRIPTOR_ID_CDW = "CDW02";
constexpr std::string_view DESCRIPTOR_ID_NSR2 = "NSR02";
constexpr std::string_view DESCRIPTOR_ID_NSR3 = "NSR03";
constexpr std::string_view DESCRIPTOR_ID_TEA = "TEA01";
const std::set<std::string_view> DESCRIPTORS = { DESCRIPTOR_ID_BEA, DESCRIPTOR_ID_BOOT2, DESCRIPTOR_ID_CDW, DESCRIPTOR_ID_NSR2, DESCRIPTOR_ID_NSR3, DESCRIPTOR_ID_TEA };


enum class TagIdentifier : uint16_t
{
    UNSPECIFIED,
    PRIMARY,
    ANCHOR_POINTER,
    POINTER,
    IMPLEMENTATION_USE,
    PARTITION,
    LOGICAL,
    UNALLOCATED_SPACE,
    TERMINATING,
    LOGICAL_INTEGRITY
};


#pragma pack(push, 1)
struct DescriptorTag
{
    TagIdentifier tag_identifier;
    uint16_t descriptor_version;
    uint8_t tag_checksum;
    uint8_t reserved;
    uint16_t tag_serial_number;
    uint16_t descriptor_crc;
    uint16_t descriptor_crc_length;
    uint32_t tag_location;
};


struct ExtentDescriptor
{
    uint32_t length;
    uint32_t location;
};


struct AnchorVolumeDescriptorPointer
{
    DescriptorTag descriptor_tag;
    ExtentDescriptor main_vds;
    ExtentDescriptor reserve_vds;
    uint8_t reserved[480];
};

struct timestamp
{
    uint16_t type_and_timezone;
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t centiseconds;
    uint8_t hundreds_of_microseconds;
    uint8_t microseconds;
};

struct charspec
{
    uint8_t character_set_type;
    uint8_t character_set_info[63];
};

struct lb_addr
{
    uint32_t logical_block_number;
    uint16_t partition_reference_number;
};

struct long_ad
{
    uint32_t extent_length;
    lb_addr extent_location;
    uint8_t implementation_use[6];
};

struct EntityID
{
    uint8_t flags;
    uint8_t identifier[23];
    uint8_t identifier_suffix[8];
};

struct LogicalVolumeDescriptor
{
    DescriptorTag descriptor_tag;
    uint32_t volume_descriptor_sequence_number;
    charspec descriptor_character_set;
    char logical_volume_identifier[128];
    uint32_t logical_block_size;
    EntityID domain_identifier;
    uint8_t logical_volume_contents_use[16];
    uint32_t map_table_length;
    uint32_t number_of_partition_maps;
    EntityID implementation_identifier;
    uint8_t implementation_use[128];
    ExtentDescriptor integrity_sequence_extent;
    uint8_t PartitionMaps[];
};

struct FileSetDescriptor
{
    DescriptorTag descriptor_tag;
    timestamp recording_date_and_time;
    uint16_t interchange_level;
    uint16_t maximum_interchange_level;
    uint32_t character_set_list;
    uint32_t maximum_character_set_list;
    uint32_t file_set_number;
    uint32_t file_set_descriptor_number;
    charspec logical_volume_identifier_character_set;
    char logical_volume_identifier[128];
    charspec file_set_character_set;
    char file_set_identifier[32];
    char copyright_file_identifier[32];
    char abstract_file_identifier[32];
    long_ad root_directory_icb;
    EntityID domain_identifier;
    long_ad next_extent;
    long_ad system_stream_directory_icb;
    uint8_t reserved[32];
};

struct icbtag
{
    uint32_t prior_recorded_number_of_direct_entries;
    uint16_t strategy_type;
    uint8_t strategy_parameter[2];
    uint16_t maximum_number_of_entries;
    uint8_t reserved;
    uint8_t file_type;
    lb_addr parent_icb_location;
    uint16_t flags;
};

struct FileEntry
{
    DescriptorTag descriptor_tag;
    icbtag icb_tag;
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    uint16_t file_link_count;
    uint8_t record_format;
    uint8_t record_display_attributes;
    uint32_t record_length;
    uint64_t information_length;
    uint64_t logical_blocks_recorded;
    timestamp access_time;
    timestamp modification_time;
    timestamp attribute_time;
    uint32_t checkpoint;
    long_ad extended_attribute_icb;
    EntityID implementation_identifier;
    uint64_t unique_id;
    uint32_t length_of_extended_attributes;
    uint32_t length_of_allocation_descriptors;
    uint8_t extended_attributes_and_allocation_descriptors[];
};

struct PartitionDescriptor
{
    DescriptorTag descriptor_tag;
    uint32_t volume_descriptor_sequence_number;
    uint16_t partition_flags;
    uint16_t partition_number;
    EntityID partition_contents;
    uint8_t partition_contents_use[128];
    uint32_t access_type;
    uint32_t partition_starting_location;
    uint32_t partition_length;
    EntityID implementation_identifier;
    uint8_t implementation_use[128];
    uint8_t reserved[156];
};
#pragma pack(pop)

}
