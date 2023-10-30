module;
#include <cstdint>

export module scsi.mmc;



namespace gpsxre
{

export enum class CDB_OperationCode : uint8_t
{
	TEST_UNIT_READY     = 0x00,
	INQUIRY             = 0x12,
	READ_CAPACITY       = 0x25,
	SYNCHRONIZE_CACHE   = 0x35,
	READ_TOC            = 0x43,
	GET_CONFIGURATION   = 0x46,
	SEND_KEY            = 0xA3,
	REPORT_KEY          = 0xA4,
	READ12              = 0xA8,
	READ_DISC_STRUCTURE = 0xAD,
	SET_CD_SPEED        = 0xBB,
	READ_CD             = 0xBE,
	READ_CDDA           = 0xD8,
	PLEXTOR_RESET       = 0xEE,
	ASUS_READ_CACHE     = 0xF1
};


export enum class INQUIRY_VPDPageCode : uint8_t
{
	SUPPORTED_PAGES,
	ASCII_INFORMATION,
	UNIT_SERIAL_NUMBER = 0x80,
	DEVICE_IDENTIFICATION = 0x83,
	SOFTWARE_INTERFACE_IDENTIFICATION,
	MANAGEMENT_NETWORK_ADDRESSES,
	EXTENDED_INQUIRY_DATA,
	MODE_PAGE_POLICY,
	SCSI_PORTS,
	POWER_CONDITION = 0x8A,
	DEVICE_CONSTITUENTS,
	CFA_PROFILE_INFORMATION,
	POWER_CONSUMPTION,
	BLOCK_LIMITS = 0xB0,
	BLOCK_DEVICE_CHARACTERISTICS,
	LOGICAL_BLOCK_PROVISIONING,
	REFERRALS,
	SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES,
	BLOCK_DEVICE_CHARACTERISTICS_EXTENSION,
	ZONED_BLOCK_DEVICE_CHARACTERISTICS,
	BLOCK_LIMITS_EXTENSION,
	FIRMWARE_NUMBERS_PAGE = 0xC0,
	DATA_CODE_PAGE,
	JUMPER_SETTINGS_PAGE,
	DEVICE_BEHAVIOR_PAGE
};


export enum class READ_TOC_ExFormat : uint8_t
{
	TOC,
	SESSION,
	FULL_TOC,
	PMA,
	ATIP,
	CD_TEXT
};


export enum class GET_CONFIGURATION_RequestedType : uint8_t
{
	ALL,
	CURRENT,
	ONE,
	RESERVED
};


export enum class GET_CONFIGURATION_FeatureCode_ProfileList : uint16_t
{
	RESERVED,
	NON_REMOVABLE_DISK,
	REMOVABLE_DISK,
	MO_ERASABLE,
	MO_WRITE_ONCE,
	AS_MO,
	
	CD_ROM = 0x08,
	CD_R,
	CD_RW,
	
	DVD_ROM = 0x10,
	DVD_R,
	DVD_RAM,
	DVD_RW,
	DVD_RW_SEQ,
	DVD_DASH_R_DL,
	DVD_DASH_R_LJ,
	DVD_PLUS_RW = 0x1A,
	DVD_PLUS_R,
	
	DDCD_ROM = 0x20,
	DDCD_R,
	DDCD_RW,
	
	DVD_PLUS_RW_DL = 0x2A,
	DVD_PLUS_R_DL,
	
	BD_ROM = 0x40,
	BD_R_SRM,
	BD_R_RRM,
	BD_RW,
	
	HDDVD_ROM = 0x50,
	HDDVD_R,
	HDDVD_RAM,
	HDDVD_RW,
	HDDVD_R_DL = 0x58,
	HDDVD_RW_DL = 0x5A,

	NON_STANDARD = 0xFFFF
};


export enum class READ_CD_ExpectedSectorType : uint8_t
{
	ALL_TYPES,   // mandatory
	CD_DA,       // optional
	MODE1,       // mandatory
	MODE2,       // mandatory
	MODE2_FORM1, // mandatory
	MODE2_FORM2, // mandatory
	RESERVED1,
	RESERVED2,

	COUNT
};


export enum class READ_CD_HeaderCode : uint8_t
{
	NONE,
	HEADER,
	SUB_HEADER,
	ALL
};


export enum class READ_CD_ErrorField : uint8_t
{
	NONE,
	C2,
	C2_BEB,
	RESERVED
};


export enum class READ_CD_SubChannel : uint8_t
{
	NONE,      // mandatory
	RAW,       // optional
	Q,         // optional
	RESERVED1,
	PW,        // optional
	RESERVED2,
	RESERVED3,
	RESERVED4
};


export enum class READ_CDDA_SubCode : uint8_t
{
	DATA,
	DATA_SUBQ,
	DATA_SUB,
	SUB,
	DATA_C2_SUB = 8
};


export enum class READ_DISC_STRUCTURE_Format : uint8_t
{
	PHYSICAL, // DI
	COPYRIGHT,
	DISC_KEY,
	BCA,
	MANUFACTURER,
	COPYRIGHT_MANAGEMENT,
	MEDIA_IDENTIFIER,
	MEDIA_KEY_BLOCK,
	DDS,
	DVD_RAM_MEDIUM_STATUS, // CARTRIDGE STATUS
	SPARE_AREA_INFORMATION,
	RESERVED1,
	RMD_LAST_BO,
	RMD,
	PRE_RECORDED_INFO_LI,
	UNIQUE_DISC_IDENTIFIER,
	PHYSICAL_LI,
	RESERVED2,
	RAW_DFL,
	RESERVED3,
	DISC_CONTROL_BLOCKS = 0x30, // PAC
	RESERVED4,
	WRITE_PROTECTION = 0xC0,
	RESERVED5,
	STRUCTURE_LIST = 0xFF
};


export enum class READ_DVD_STRUCTURE_CopyrightInformation_CPST : uint8_t
{
	NONE,
	CSS_CPPM,
	CPRM
};

export enum class SEND_KEY_KeyFormat : uint8_t
{
	CHALLENGE_KEY = 0x01,
	KEY2 = 0x03,
	RPC_STRUCTURE = 0x06,
	INVALIDATE_AGID = 0x3F
};


export enum class REPORT_KEY_KeyClass : uint8_t
{
	DVD_CSS_CPPM_CPRM,
	REWRITABLE_SECURITY_SERVICE_A
};


export enum class REPORT_KEY_KeyFormat : uint8_t
{
	AGID,
	CHALLENGE_KEY,
	KEY1,
	TITLE_KEY = 0x04,
	ASF,
	RPC_STATE = 0x08,
	AGID_CPRM = 0x11,
	INVALIDATE_AGID = 0x3F
};


export struct CMD_ParameterListHeader
{
	uint16_t data_length;
	uint8_t fields[2];
};


export struct TOC_Descriptor
{
	uint8_t reserved1;
	uint8_t control               :4;
	uint8_t adr                   :4;
	uint8_t track_number;
	uint8_t reserved2;
	uint32_t track_start_address;
};


export struct FULL_TOC_Descriptor
{
	uint8_t session_number;
	uint8_t control         :4;
	uint8_t adr             :4;
	uint8_t tno;
	uint8_t point;
	uint8_t msf[3];
	uint8_t zero;
	uint8_t p_msf[3];
};


export struct CD_TEXT_Descriptor
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


export struct REPORT_KEY_AGID
{
	uint8_t reserved1;
	uint8_t reserved2;
	uint8_t reserved3;
	uint8_t reserved4   :6;
	uint8_t agid        :2;
};


export struct REPORT_KEY_ASF
{
	uint8_t reserved1;
	uint8_t reserved2;
	uint8_t reserved3;
	uint8_t asf : 1;
	uint8_t reserved4 : 7;
};


export struct REPORT_KEY_TitleKey
{
	uint8_t cp_mod        :4;
	uint8_t cgms          :2;
	uint8_t cp_sec        :1;
	uint8_t cpm           :1;
	uint8_t title_key[5];
	uint8_t reserved1;
	uint8_t reserved2;
};


export struct REPORT_KEY_Key
{
	uint8_t key[5];
	uint8_t reserved[3];
};


export struct REPORT_KEY_ChallengeKey
{
	uint8_t challenge[10];
	uint8_t reserved[2];
};


export struct GET_CONFIGURATION_FeatureHeader
{
	uint32_t data_length;
	uint8_t reserved1;
	uint8_t reserved2;
	uint16_t current_profile;
};


export struct GET_CONFIGURATION_FeatureDescriptor
{
	uint16_t feature_code;
	uint8_t current                    :1;
	uint8_t persistent                 :1;
	uint8_t version                    :4;
	uint8_t reserved                   :2;
	uint8_t additional_length;
	uint8_t feature_dependent_data[0];
};


export struct READ_DVD_STRUCTURE_StructureListEntry
{
	uint8_t format_code;
	uint8_t reserved           :6;
	uint8_t rds                :1;
	uint8_t sds                :1;
	uint16_t structure_length;
};


export struct READ_DVD_STRUCTURE_LayerDescriptor
{
	uint8_t part_version          :4;
	uint8_t book_type             :4;
	uint8_t maximum_rate          :4;
	uint8_t disc_size             :4;
	uint8_t layer_type            :4;
	uint8_t track_path            :1;
	uint8_t layers_number         :2;
	uint8_t reserved1             :1;
	uint8_t track_density         :4;
	uint8_t linear_density        :4;
	uint32_t data_start_sector;
	uint32_t data_end_sector;
	uint32_t layer0_end_sector;
	uint8_t reserved2             :7;
	uint8_t bca                   :1;
	uint8_t media_specific[2031];
};


export struct READ_DVD_STRUCTURE_CopyrightInformation
{
	uint8_t copyright_protection_system_type;
	uint8_t region_management_information;
	uint8_t reserved1;
	uint8_t reserved2;
};

#pragma pack(push, 1)
export struct INQUIRY_StandardData
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
	uint8_t addr16                     :1;
	uint8_t addr32                     :1;
	uint8_t ack_req_q                  :1;
	uint8_t medium_changer             :1;
	uint8_t multi_port                 :1;
	uint8_t reserved2                  :1;
	uint8_t enclosure_services         :1;
	uint8_t reserved3                  :1;
	uint8_t soft_reset                 :1;
	uint8_t command_queue              :1;
	uint8_t transfer_disable           :1;
	uint8_t linked_commands            :1;
	uint8_t synchronous                :1;
	uint8_t wide_16bit                 :1;
	uint8_t wide_32bit                 :1;
	uint8_t relative_addressing        :1;
	uint8_t vendor_id[8];
	uint8_t product_id[16];
	uint8_t product_revision_level[4];
	uint8_t vendor_specific[20];
	uint8_t reserved4[2];
	uint16_t version_descriptors[8];
	uint8_t reserved5[30];
};
#pragma pack(pop)


export struct INQUIRY_VPDBlockLimits
{
	uint8_t peripheral_device_type                                :5;
	uint8_t peripheral_qualifier                                  :3;
	uint8_t page_code;
	uint16_t page_length;
	uint8_t reserved1;
	uint8_t maximum_compare_and_write_length;
	uint16_t optimal_transfer_length_granularity;
	uint32_t maximum_transfer_length;
	uint32_t optimal_transfer_length;
	uint32_t maximum_prefetch_length;
	uint32_t maximum_unmap_lba_count;
	uint32_t maximum_unmap_block_descriptor_count;
	uint32_t optimal_unmap_granularity;
	uint32_t ugavalid                                             :1;
	uint32_t unmap_granularity_alignment                          :31;
	uint32_t maximum_write_same_length;
	uint32_t reserved2;
	uint32_t maximum_atomic_transfer_length;
	uint32_t atomic_alignment;
	uint32_t atomic_transfer_length_granularity;
	uint32_t maximum_atomic_transfer_length_with_atomic_boundary;
	uint32_t maximum_atomic_boundary_size;
};


export struct READ_CAPACITY_Response
{
	uint32_t address;
	uint32_t block_length;
};


export struct CDB6_Generic
{
	uint8_t  operation_code;
	uint8_t  immediate                :1;
	uint8_t  command_unique_bits      :4;
	uint8_t  logical_unit_number      :3;
	uint8_t  command_unique_bytes[3];
	uint8_t  link                     :1;
	uint8_t  flag                     :1;
	uint8_t  reserved                 :4;
	uint8_t  vendor_unique            :2;
};


export struct CDB6_Inquiry
{
	uint8_t operation_code;
	uint8_t enable_vital_product_data :1;
	uint8_t command_support_data      :1;
	uint8_t reserved1                 :6;
	uint8_t page_code;
	uint8_t allocation_length[2];     // unaligned
	uint8_t control;
};


export struct CDB10_ReadCapacity
{
	uint8_t operation_code;
	uint8_t rel_adr         :1;
	uint8_t reserved2       :4;
	uint8_t reserved1       :3;
	uint8_t address[4];
	uint8_t reserved3;
	uint8_t reserved4;
	uint8_t pmi             :1;
	uint8_t reserved5       :7;
	uint8_t control;
};


export struct CDB10_ReadTOC
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


#pragma pack(push, 1)
export struct CDB10_ASUS_ReadCache
{
	uint8_t operation_code;
	uint8_t unknown;
	uint32_t offset;
	uint32_t size;
};
#pragma pack(pop)


export struct CDB10_GetConfiguration
{
	uint8_t operation_code;
	uint8_t requested_type            :2;
	uint8_t reserved2                 :3;
	uint8_t reserved1                 :3;
	uint16_t starting_feature_number;
	uint8_t reserved3;
	uint8_t reserved4;
	uint8_t reserved5;
	uint8_t allocation_length[2];     // unaligned
	uint8_t control;
};


export struct CDB12_ReadDiscStructure
{
	uint8_t operation_code;
	uint8_t media_type            :4;
	uint8_t reserved1             :4;
	uint8_t address[4];
	uint8_t layer_number;
	uint8_t format;
	uint8_t allocation_length[2];
	uint8_t reserved2             :6;
	uint8_t agid                  :2;
	uint8_t control;
};


export struct CDB12_SendKey
{
	uint8_t operation_code;
	uint8_t reserved2               :5;
	uint8_t reserved1               :3;
	uint8_t reserved3[6];
	uint16_t parameter_list_length;
	uint8_t key_format              :6;
	uint8_t agid                    :2;
	uint8_t control;
};


export struct CDB12_ReportKey
{
	uint8_t operation_code;
	uint8_t reserved2             :5;
	uint8_t reserved1             :3;
	uint8_t lba[4];
	uint8_t reserved3;
	uint8_t key_class;
	uint8_t allocation_length[2];
	uint8_t key_format            :6;
	uint8_t agid                  :2;
	uint8_t control;
};


export struct CDB12_Read
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


export struct CDB12_ReadCD
{
	uint8_t operation_code;
	uint8_t relative_address      :1;
	uint8_t reserved1             :1;
	uint8_t expected_sector_type  :3;
	uint8_t lun                   :3;
	uint8_t starting_lba[4];
	uint8_t transfer_blocks[3];
	uint8_t reserved2             :1;
	uint8_t error_flags           :2;
	uint8_t include_edc           :1;
	uint8_t include_user_data     :1;
	uint8_t header_code           :2;
	uint8_t include_sync_data     :1;
	uint8_t sub_channel_selection :3;
	uint8_t reserved3             :5;
	uint8_t control;
};


export struct CDB12_ReadCDDA
{
	uint8_t operation_code;
	uint8_t reserved1           :5;
	uint8_t lun                 :3;
	uint8_t starting_lba[4];
	uint8_t transfer_blocks[4];
	uint8_t sub_code;
	uint8_t control;
};


export struct CDB12_SetCDSpeed
{
	uint8_t operation_code;
	union
	{
		uint8_t reserved1;
		struct
		{
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
