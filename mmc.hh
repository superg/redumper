#pragma once



#include <cstdint>



namespace gpsxre
{

enum class CDB_OperationCode : uint8_t
{
    TEST_UNIT_READY   = 0x00,
    INQUIRY           = 0x12,
    SYNCHRONIZE_CACHE = 0x35,
    READ_TOC          = 0x43,
    GET_CONFIGURATION = 0x46,
    READ12            = 0xA8,
    SET_CD_SPEED      = 0xBB,
    READ_CD           = 0xBE,
    READ_CDDA         = 0xD8,
    PLEXTOR_RESET     = 0xEE,
    ASUS_READ_CACHE   = 0xF1
};


enum class READ_TOC_ExFormat : uint8_t
{
    TOC      = 0x00,
    SESSION  = 0x01,
    FULL_TOC = 0x02,
    PMA      = 0x03,
    ATIP     = 0x04,
    CD_TEXT  = 0x05
};


enum class GET_CONFIGURATION_RequestedType : uint8_t
{
    ALL      = 0x00,
    CURRENT  = 0x01,
    ONE      = 0x02,
    RESERVED = 0x03
};


enum class READ_CD_ExpectedSectorType : uint8_t
{
    ALL_TYPES   = 0, // mandatory
    CD_DA       = 1, // optional
    MODE1       = 2, // mandatory
    MODE2       = 3, // mandatory
    MODE2_FORM1 = 4, // mandatory
    MODE2_FORM2 = 5, // mandatory
    RESERVED1   = 6,
    RESERVED2   = 7
};


enum class READ_CD_HeaderCode : uint8_t
{
    NONE       = 0,
    HEADER     = 1,
    SUB_HEADER = 2,
    ALL        = 3
};


enum class READ_CD_ErrorField : uint8_t
{
    NONE     = 0,
    C2       = 1,
    C2_BEB   = 2,
    RESERVED = 3
};


enum class READ_CD_SubChannel : uint8_t
{
    NONE      = 0, // mandatory
    RAW       = 1, // optional
    Q         = 2, // optional
    RESERVED1 = 3,
    PW        = 4, // optional
    RESERVED2 = 5,
    RESERVED3 = 6,
    RESERVED4 = 7
};


enum class READ_CDDA_SubCode : uint8_t
{
	DATA = 0,
	DATA_SUBQ = 1,
	DATA_SUB = 2,
	SUB = 3,
	DATA_C2_SUB = 8
};


struct READ_TOC_Response
{
    uint16_t data_length;
    uint8_t fields[2];
};


struct TOC_Descriptor
{
    uint8_t reserved1;
    uint8_t control :4;
    uint8_t adr     :4;
    uint8_t track_number;
    uint8_t reserved2;
    uint32_t track_start_address;
};


struct FULL_TOC_Descriptor
{
    uint8_t session_number;
    uint8_t control      : 4;
    uint8_t adr          : 4;
    uint8_t tno;
    uint8_t point;
    uint8_t msf[3];
    uint8_t zero;
    uint8_t p_msf[3];
};


struct CD_TEXT_Descriptor
{
    uint8_t pack_type;
    uint8_t track_number       :7;
    uint8_t extension_flag     :1;
    uint8_t sequence_number;
    uint8_t character_position :4;
    uint8_t block_number       :3;
    uint8_t unicode            :1;
    uint8_t text[12];
    uint16_t crc;
};


struct GET_CONFIGURATION_FeatureHeader
{
    uint32_t data_length;
    uint8_t reserved1;
    uint8_t reserved2;
    uint16_t current_profile;
};


struct GET_CONFIGURATION_FeatureDescriptor
{
    uint16_t feature_code;
    uint8_t current    :1;
    uint8_t persistent :1;
    uint8_t version    :4;
    uint8_t reserved   :2;
    uint8_t additional_length;
    uint8_t feature_dependent_data[0];
};


#pragma pack(push, 1)
struct CDB_ASUS_ReadCache
{
    uint8_t operation_code;
    uint8_t unknown;
    uint32_t offset;
    uint32_t size;
};
#pragma pack(pop)


struct CDB6_Generic
{
    uint8_t  operation_code;
    uint8_t  immediate            :1;
    uint8_t  command_unique_bits  :4;
    uint8_t  logical_unit_number  :3;
    uint8_t  command_unique_bytes[3];
    uint8_t  link                 :1;
    uint8_t  flag                 :1;
    uint8_t  reserved             :4;
    uint8_t  vendor_unique        :2;
};


struct CDB6_Inquiry3
{
    uint8_t operation_code;
    uint8_t enable_vital_product_data :1;
    uint8_t command_support_data      :1;
    uint8_t reserved1                 :6;
    uint8_t page_code;
    uint8_t reserved2;
    uint8_t allocation_length;
    uint8_t control;
};


#pragma pack(push, 1)
struct InquiryData
{
    uint8_t device_type           :5;
    uint8_t device_type_qualifier :3;
    uint8_t device_type_modifier  :7;
    uint8_t removable_media       :1;
    union
    {
        uint8_t versions;
        struct
        {
            uint8_t ansi_version :3;
            uint8_t ecma_version :3;
            uint8_t iso_version  :2;
        };
    };
    uint8_t response_data_format :4;
    uint8_t hi_support           :1;
    uint8_t norm_aca             :1;
    uint8_t terminate_task       :1;
    uint8_t aerc                 :1;
    uint8_t additional_length;
    union
    {
        uint8_t reserved;
        struct
        {
            uint8_t protect          :1;
            uint8_t reserved1        :2;
            uint8_t third_party_copy :1;
            uint8_t tpgs             :2;
            uint8_t acc              :1;
            uint8_t sccs             :1;
        };
    };
    uint8_t addr16              :1;
    uint8_t addr32              :1;
    uint8_t ack_req_q           :1;
    uint8_t medium_changer      :1;
    uint8_t multi_port          :1;
    uint8_t reserved2           :1;
    uint8_t enclosure_services  :1;
    uint8_t reserved3           :1;
    uint8_t soft_reset          :1;
    uint8_t command_queue       :1;
    uint8_t transfer_disable    :1;
    uint8_t linked_commands     :1;
    uint8_t synchronous         :1;
    uint8_t wide_16bit          :1;
    uint8_t wide_32bit          :1;
    uint8_t relative_addressing :1;
    uint8_t vendor_id[8];
    uint8_t product_id[16];
    uint8_t product_revision_level[4];
    uint8_t vendor_specific[20];
    uint8_t reserved4[2];
    uint16_t version_descriptors[8];
    uint8_t reserved5[30];
};
#pragma pack(pop)


struct CDB10_ReadTOC
{
    uint8_t operation_code;
    uint8_t reserved1             :1;
    uint8_t msf                   :1;
    uint8_t reserved2             :3;
    uint8_t logical_unit_number   :3;
    uint8_t format2               :4;
    uint8_t reserved3             :4;
    uint8_t reserved4[3];
    uint8_t starting_track;
    uint8_t allocation_length[2]; // unaligned
    uint8_t control               :6;
    uint8_t format                :2;
};


struct CDB10_GetConfiguration
{
    uint8_t operation_code;
    uint8_t requested_type        :2;
    uint8_t reserved2             :3;
    uint8_t reserved1             :3;
    uint16_t starting_feature_number;
    uint8_t reserved3;
    uint8_t reserved4;
    uint8_t reserved5;
    uint8_t allocation_length[2]; // unaligned
    uint8_t control;
};


struct CDB12_ReadCD
{
    uint8_t operation_code;
    uint8_t relative_address : 1;
    uint8_t reserved1 : 1;
    uint8_t expected_sector_type : 3;
    uint8_t lun : 3;
    uint8_t starting_lba[4];
    uint8_t transfer_blocks[3];
    uint8_t reserved2 : 1;
    uint8_t error_flags : 2;
    uint8_t include_edc : 1;
    uint8_t include_user_data : 1;
    uint8_t header_code : 2;
    uint8_t include_sync_data : 1;
    uint8_t sub_channel_selection : 3;
    uint8_t reserved3 : 5;
    uint8_t control;
};


struct CDB12_ReadCDDA
{
    uint8_t operation_code;
    uint8_t reserved1           :5;
    uint8_t lun                 :3;
    uint8_t starting_lba[4];
    uint8_t transfer_blocks[4];
    uint8_t sub_code;
    uint8_t control;
};


struct CDB12_Read
{
    uint8_t operation_code;
    uint8_t relative_address    :1;
    uint8_t reserved1           :2;
    uint8_t force_unit_access   :1;
    uint8_t disable_page_out    :1;
    uint8_t lun                 :3;
    uint8_t starting_lba[4];
    uint8_t transfer_blocks[4];
    uint8_t reserved2           :7;
    uint8_t streaming           :1;
    uint8_t control;
};

struct CDB12_SetCDSpeed
{
    uint8_t operation_code;
    union
    {
        uint8_t reserved1;
        struct {
            uint8_t rotation_control :2;
            uint8_t reserved2        :6;
        };
    };
    uint8_t read_speed[2];
    uint8_t write_speed[2];
    uint8_t reserved3[5];
    uint8_t control;
};

}
