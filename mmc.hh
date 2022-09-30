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


struct CDB12_PLEXTOR_ReadCDDA
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
