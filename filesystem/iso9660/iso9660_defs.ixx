module;
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660:defs;

import cd.cdrom;
import readers.sector_reader;
import utils.endian;
import utils.misc;
import utils.strings;



export namespace gpsxre::iso9660
{

constexpr uint32_t SYSTEM_AREA_SIZE = 16;
constexpr uint8_t STANDARD_IDENTIFIER[] = "CD001";
constexpr uint8_t STANDARD_IDENTIFIER_CDI[] = "CD-I ";


enum class VolumeDescriptorType : uint8_t
{
	BOOT_RECORD = 0,
	PRIMARY = 1,
	SUPPLEMENTARY = 2,
	PARTITION = 3,
	RESERVED_4 = 4,
	RESERVED_254 = 254,
	SET_TERMINATOR = 255
};


enum class Characters : char
{
	DIR_CURRENT = '\0',
	DIR_PARENT = '\1',
	SEPARATOR1 = '.',
	SEPARATOR2 = ';'
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


struct PathRecord
{
	uint8_t length;
	uint8_t xa_length;
	uint32_t offset;
	uint16_t parent_directory_number;
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
	VolumeDescriptorType type;
	uint8_t standard_identifier[5];
	uint8_t version;
	uint8_t data[2041];
};


struct PrimaryVolumeDescriptor
{
	VolumeDescriptorType type;
	uint8_t standard_identifier[5];
	uint8_t version;
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
	uint32_t path_table_l_offset;
	uint32_t path_table_l_offset_opt;
	uint32_t path_table_m_offset;
	uint32_t path_table_m_offset_opt;
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
};
#pragma pack(pop)


int ascii_to_decimal(const uint8_t *data, std::size_t size)
{
	int decimal = 0;

	for(std::size_t i = 0; i < size; ++i)
	{
		uint8_t digit = (char)data[i] - '0';
		if(digit > 9)
			digit = 0;
		decimal = decimal * 10 + digit;
	}

	return decimal;
}


time_t convert_time(const DateTime &date_time)
{
	tm time_info;

	int year = ascii_to_decimal(date_time.year, sizeof(date_time.year));
	// PSX specifics
	if(year < 1970)
	{
		if(year >= 1900)
			year -= 1900;
		year += 2000;
	}
	time_info.tm_year = year - 1900;
	time_info.tm_mon = ascii_to_decimal(date_time.month, sizeof(date_time.month)) - 1;
	time_info.tm_mday = ascii_to_decimal(date_time.day, sizeof(date_time.day));
	time_info.tm_hour = ascii_to_decimal(date_time.hour, sizeof(date_time.hour));
	time_info.tm_min = ascii_to_decimal(date_time.minute, sizeof(date_time.minute));
	time_info.tm_sec = ascii_to_decimal(date_time.second, sizeof(date_time.second));
	time_info.tm_isdst = -1;

	// FIXME: GMT offset

	return mktime(&time_info);
}


time_t convert_time(const RecordingDateTime &date_time)
{
	tm time_info;

	// PSX specifics
	uint32_t year = date_time.year;
	if(year < 70)
		year += 100;

	time_info.tm_year = year;
	time_info.tm_mon = date_time.month - 1;
	time_info.tm_mday = date_time.day;
	time_info.tm_hour = date_time.hour;
	time_info.tm_min = date_time.minute;
	time_info.tm_sec = date_time.second;
	time_info.tm_isdst = -1;

	// FIXME: GMT offset

	return mktime(&time_info);
}


bool directory_record_is_valid(const DirectoryRecord &dr)
{
	return dr.offset.lsb == endian_swap(dr.offset.msb) && dr.data_length.lsb == endian_swap(dr.data_length.msb);
}


std::vector<std::pair<std::string, DirectoryRecord>> directory_extent_get_records(const std::vector<uint8_t> &extent)
{
	std::vector<std::pair<std::string, DirectoryRecord>> directory_records;

	for(uint32_t i = 0, n = (uint32_t)extent.size(); i < n;)
	{
		auto dr = (DirectoryRecord &)extent[i];

		uint32_t next_sector = round_down(i, FORM1_DATA_SIZE) + FORM1_DATA_SIZE;
		if(dr.length && dr.length <= next_sector - i)
		{
			// (1) [01/12/2020]: "All Star Racing 2 (Europe) (Track 1).bin"
			// (2) [11/10/2020]: "All Star Racing 2 (USA) (Track 1).bin"
			// (3) [01/21/2020]: "Aitakute... - Your Smiles in My Heart - Oroshitate no Diary - Introduction Disc (Japan) (Track 1).bin"
			// (4) [01/21/2020]: "MLB 2005 (USA).bin"
			// all these tracks have messed up directory records, (1), (2) and (4) have garbage after valid entries, (3) has garbage before
			// good DirectoryRecord validity trick is to compare lsb to msb for offset and data_length and make sure it's the same
			if(!directory_record_is_valid(dr))
			{
				// FIXME:
				//  1) try to find legit entry after garbage, useful for (3)
				//				++i;
				//				continue;
				// 2) skip everything
				break;
			}

			std::string identifier((const char *)&extent[i + sizeof(dr)], dr.file_identifier_length);
			directory_records.emplace_back(identifier, dr);

			i += dr.length;
		}
		// skip sector boundary
		else
			i = next_sector;
	}

	return directory_records;
}


uint32_t directory_extent_get_length(SectorReader *sector_reader, uint32_t offset)
{
	std::vector<uint8_t> sector(sector_reader->sectorSize());
	if(sector_reader->read(sector.data(), offset, 1) == 1)
	{
		auto dr = (DirectoryRecord &)sector[0];
		return dr.data_length.lsb;
	}

	return 0;
}


std::string split_identifier(uint32_t &version, std::string identifier)
{
	auto s = identifier.find_last_of((char)iso9660::Characters::SEPARATOR2);

	version = (s == std::string::npos ? 0 : str_to_int(identifier.substr(s + 1)));

	return identifier.substr(0, s);
}

}
