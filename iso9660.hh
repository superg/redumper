#pragma once



#include <cstdint>
#include <ctime>



namespace iso9660
{

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
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t gmt_offset;
};

struct DirectoryRecord
{
	enum class FileFlags : uint8_t
	{
		EXISTENCE       = 1 << 0,
		DIRECTORY       = 1 << 1,
		ASSOCIATED_FILE = 1 << 2,
		RECORD          = 1 << 3,
		PROTECTION      = 1 << 4,
		RESERVED1       = 1 << 5,
		RESERVED2       = 1 << 6,
		MULTI_EXTENT    = 1 << 7
	};

	uint8_t length;
	uint8_t xa_length;
	uint64_lsb_msb offset;
	uint64_lsb_msb data_length;
	RecordingDateTime recording_date_time;
	uint8_t file_flags;
	uint8_t file_unit_size;
	uint8_t interleave_gap_size;
	uint32_t volume_sequence_number;
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
	int8_t gmt_offset;
};

struct VolumeDescriptor
{
	enum class Type : uint8_t
	{
		BOOT_RECORD    = 0,
		PRIMARY        = 1,
		SUPPLEMENTARY  = 2,
		PARTITION      = 3,
		RESERVED_4     = 4,
		RESERVED_254   = 254,
		SET_TERMINATOR = 255
	} type;
	uint8_t standard_identifier[5];
	uint8_t version;

	union
	{
		uint8_t data[2041];
		struct
		{
			uint8_t unused1;
			char system_identifier[32];
			char volume_identifier[32];
			uint8_t unused2[8];
			uint64_lsb_msb volume_space_size;
			uint8_t unused3[32];
			uint32_lsb_msb volume_set_size;
			uint32_lsb_msb volume_sequence_number;
			uint32_lsb_msb logical_block_size;
			uint64_lsb_msb path_table_size;
			uint32_t type_l_path_table_offset;
			uint32_t optional_type_l_path_table_offset;
			uint32_t type_m_path_table_offset;
			uint32_t optional_type_m_path_table_offset;
			DirectoryRecord root_directory_record;
			uint8_t root_directory_identifier;
			char volume_set_identifier[128];
			char publisher_identifier[128];
			char data_preparer_identifier[128];
			char application_identifier[128];
			char copyright_file_identifier[37];
			char abstract_file_identifier[37];
			char bibliographic_file_identifier[37];
			DateTime volume_creation_date_time;
			DateTime volume_modification_date_time;
			DateTime volume_expiration_date_time;
			DateTime volume_effective_date_time;
			uint8_t file_structure_version;
			uint8_t reserved1;
			uint8_t application_use[512];
			uint8_t reserved2[653];
		} primary;
	};
};

struct PathRecord
{
	uint8_t length;
	uint8_t xa_length;
	uint32_t offset;
	uint16_t parent_directory_number;
};
#pragma pack(pop)

enum class Characters : char
{
	DIR_CURRENT = '\0',
	DIR_PARENT = '\1',
	SEPARATOR1 = '.',
	SEPARATOR2 = ';'
};

constexpr uint32_t SYSTEM_AREA_SIZE = 16;
constexpr uint8_t STANDARD_IDENTIFIER[] = "CD001";
constexpr uint8_t CDI_STANDARD_IDENTIFIER[] = "CD-I ";

time_t convert_time(const DateTime &date_time);
time_t convert_time(const RecordingDateTime &date_time);

}
