module;
#include <cstdint>

export module filesystem.high_sierra:defs;



export namespace gpsxre::high_sierra
{

constexpr uint32_t SYSTEM_AREA_SIZE = 16;
constexpr uint8_t STANDARD_IDENTIFIER[] = "CDROM";


enum class VolumeDescriptorType : uint8_t
{
    BOOT_RECORD,
    STANDARD_FS,
    CODED_CHARACTER_SET_FS,
    UNSPECIFIED_STRUCTURE,
    RESERVED_4,
    SET_TERMINATOR = 255
};


#pragma pack(push, 1)
struct uint64_lsb_msb
{
    uint32_t lsb;
    uint32_t msb;
};


struct uint32_lsb_msb
{
    uint16_t lsb;
    uint16_t msb;
};


struct RecordingDateTime
{
    uint8_t year_since_1900;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};


struct DirectoryRecord
{
    enum class FileFlags : uint8_t
    {
        EXISTENCE = 1 << 0,
        DIRECTORY = 1 << 1,
        ASSOCIATED_FILE = 1 << 2,
        RECORD = 1 << 3,
        PROTECTION = 1 << 4,
        RESERVED1 = 1 << 5,
        RESERVED2 = 1 << 6,
        MULTI_EXTENT = 1 << 7
    };

    uint8_t length;
    uint8_t length_xa;
    uint64_lsb_msb extent_location;
    uint64_lsb_msb data_length;
    RecordingDateTime recording_date_time;
    uint8_t file_flags;
    uint8_t reserved1;
    uint8_t interleave_size;
    uint8_t interleave_skip_factor;
    uint32_t volume_set_sequence_number;
    uint8_t file_identifier_length;
};


struct DateTime
{
    uint8_t year[4];
    uint8_t month[2];
    uint8_t day[2];
    uint8_t hour[2];
    uint8_t minute[2];
    uint8_t second[2];
    uint8_t centisecond[2];
};


struct VolumeDescriptorBase
{
    uint64_lsb_msb lbn;
    VolumeDescriptorType type;
    uint8_t standard_identifier[5];
    uint8_t standard_version;
};


struct VolumeDescriptor
{
    VolumeDescriptorBase vd;
    uint8_t data[2033];
};


struct StandardFileStructureVolumeDescriptor
{
    VolumeDescriptorBase vd;
    uint8_t reserved1;
    char system_identifier[32];
    char volume_identifier[32];
    uint8_t reserved2[8];
    uint64_lsb_msb volume_space_size;
    uint8_t reserved3[32];
    uint32_lsb_msb volume_set_size;
    uint32_lsb_msb volume_set_sequence_number;
    uint32_lsb_msb logical_block_size;
    uint64_lsb_msb path_table_size;
    uint32_t path_table_first_offset[4];
    uint32_t path_table_second_offset[4];
    DirectoryRecord root_directory_record;
    uint8_t root_directory_identifier;
    char volume_set_identifier[128];
    char publisher_identifier[128];
    char data_preparer_identifier[128];
    char application_identifier[128];
    char copyright_file_identifier[32];
    char abstract_file_identifier[32];
    DateTime volume_creation_date_time;
    DateTime volume_modification_date_time;
    DateTime volume_expiration_date_time;
    DateTime volume_effective_date_time;
    uint8_t file_structure_standard_version;
    uint8_t reserved4;
    uint8_t application_use[512];
    uint8_t future_standardization[680];
};
#pragma pack(pop)

}
