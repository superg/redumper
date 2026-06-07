module;
#include <cstdint>
#include <string_view>

export module filesystem.apm:defs;



export namespace gpsxre::apm
{

// Based on https://keramics.github.io/apm.html

constexpr std::string_view DRIVE_DESCRIPTOR_SIGNATURE = "ER";
constexpr std::string_view PARTITION_MAP_ENTRY_SIGNATURE = "PM";
constexpr uint32_t PARTITION_MAP_OFFSET = 1;

constexpr std::string_view PARTITION_TYPE_APPLE_BOOT = "Apple_Boot";
constexpr std::string_view PARTITION_TYPE_APPLE_BOOT_RAID = "Apple_Boot_RAID";
constexpr std::string_view PARTITION_TYPE_APPLE_BOOTSTRAP = "Apple_Bootstrap";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER = "Apple_Driver";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER43 = "Apple_Driver43";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER43_CD = "Apple_Driver43_CD";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER_ATA = "Apple_Driver_ATA";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER_ATAPI = "Apple_Driver_ATAPI";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER_IOKIT = "Apple_Driver_IOKit";
constexpr std::string_view PARTITION_TYPE_APPLE_DRIVER_OPENFIRMWARE = "Apple_Driver_OpenFirmware";
constexpr std::string_view PARTITION_TYPE_APPLE_EXTRA = "Apple_Extra";
constexpr std::string_view PARTITION_TYPE_APPLE_FREE = "Apple_Free";
constexpr std::string_view PARTITION_TYPE_APPLE_FWDRIVER = "Apple_FWDriver";
constexpr std::string_view PARTITION_TYPE_APPLE_HFS = "Apple_HFS";
constexpr std::string_view PARTITION_TYPE_APPLE_HFSX = "Apple_HFSX";
constexpr std::string_view PARTITION_TYPE_APPLE_LOADER = "Apple_Loader";
constexpr std::string_view PARTITION_TYPE_APPLE_MDFW = "Apple_MDFW";
constexpr std::string_view PARTITION_TYPE_APPLE_MFS = "Apple_MFS";
constexpr std::string_view PARTITION_TYPE_APPLE_PARTITION_MAP = "Apple_partition_map";
constexpr std::string_view PARTITION_TYPE_APPLE_PATCHES = "Apple_Patches";
constexpr std::string_view PARTITION_TYPE_APPLE_PRODOS = "Apple_PRODOS";
constexpr std::string_view PARTITION_TYPE_APPLE_RAID = "Apple_RAID";
constexpr std::string_view PARTITION_TYPE_APPLE_RHAPSODY_UFS = "Apple_Rhapsody_UFS";
constexpr std::string_view PARTITION_TYPE_APPLE_SCRATCH = "Apple_Scratch";
constexpr std::string_view PARTITION_TYPE_APPLE_SECOND = "Apple_Second";
constexpr std::string_view PARTITION_TYPE_APPLE_UFS = "Apple_UFS";
constexpr std::string_view PARTITION_TYPE_APPLE_UNIX_SVR2 = "Apple_UNIX_SVR2";
constexpr std::string_view PARTITION_TYPE_APPLE_VOID = "Apple_Void";
constexpr std::string_view PARTITION_TYPE_BE_BFS = "Be_BFS";
constexpr std::string_view PARTITION_TYPE_MFS = "MFS";


enum StatusFlags : uint32_t
{
    VALID = 1u << 0,
    ALLOCATED = 1u << 1,
    IN_USE = 1u << 2,
    HAS_BOOT_INFO = 1u << 3,
    READABLE = 1u << 4,
    WRITABLE = 1u << 5,
    PIC_BOOT_CODE = 1u << 6,

    HAS_COMPATIBLE_DRIVER = 1u << 8,
    HAS_REAL_DRIVER = 1u << 9,
    HAS_CHAIN_DRIVER = 1u << 10,

    AUTOMATIC_MOUNT = 1u << 30,
    STARTUP_PARTITION = 1u << 31,
};


struct DeviceDriverDescriptor
{
    uint32_t start_block;
    uint16_t blocks;
    uint16_t os;
};


struct DriveDescriptor
{
    uint8_t signature[2];
    uint16_t block_size;
    uint32_t block_count;
    uint16_t device_type;
    uint16_t device_identifier;
    uint32_t device_data;
    uint16_t driver_descriptor_count;
    uint8_t first_driver_descriptor[8];
    uint8_t additional_driver_descriptors[484];

};


struct PartitionMapEntry
{
    uint8_t signature[2];
    uint8_t reserved[2];
    uint32_t entry_count;
    uint32_t partition_start_sector;
    uint32_t partition_sector_count;
    char partition_name[32];
    char partition_type[32];
    uint32_t data_area_start_sector;
    uint32_t data_area_sector_count;
    StatusFlags status_flags;
    uint32_t boot_code_start_sector;
    uint32_t boot_code_sector_count;
    uint32_t boot_code_address;
    uint32_t unknown_1;
    uint32_t boot_code_entry_point;
    uint32_t unknown_2;
    uint32_t boot_code_checksum;
    uint8_t processor_type[16];
    uint8_t unknown_3[2][188];
};

}
