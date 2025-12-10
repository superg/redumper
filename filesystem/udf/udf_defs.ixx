module;
#include <cstdint>

export module filesystem.udf:defs;



export namespace gpsxre::udf
{

constexpr uint32_t AVDP_PRIMARY_LBA = 256;


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


struct PartitionDescriptor
{
    DescriptorTag descriptor_tag;
    uint32_t volume_descriptor_sequence_number;
    uint16_t partition_flags;
    uint16_t partition_number;
    uint8_t partition_contents[32];
    uint8_t partition_contents_use[128];
    uint32_t access_type;
    uint32_t partition_starting_location;
    uint32_t partition_length;
    uint8_t implementation_identifier[32];
    uint8_t implementation_use[128];
    uint8_t reserved[156];
};
#pragma pack(pop)

}
