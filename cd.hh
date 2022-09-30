#pragma once



#include <cstdint>



namespace gpsxre
{

struct MSF
{
	union
	{
		struct
		{
			uint8_t m;
			uint8_t s;
			uint8_t f;
		};
		uint8_t raw[3];
	};
};

constexpr uint32_t FORM1_DATA_SIZE = 2048;
constexpr uint32_t FORM2_DATA_SIZE = 2324;

enum class CDXAMode : uint8_t
{
	EORECORD = 1 << 0,
	VIDEO    = 1 << 1,
	AUDIO    = 1 << 2,
	DATA     = 1 << 3,
	TRIGGER  = 1 << 4,
	FORM2    = 1 << 5,
	REALTIME = 1 << 6,
	EOFILE   = 1 << 7
};

struct Sector
{
	struct ECC
	{
		uint8_t p_parity[172];
		uint8_t q_parity[104];
	};

	uint8_t sync[12];

	struct Header
	{
		MSF address;
		uint8_t mode;
	} header;

	union
	{
		struct
		{
			uint8_t user_data[FORM1_DATA_SIZE];
			uint32_t edc;
			uint8_t intermediate[8];
			ECC ecc;
		} mode1;
		struct
		{
			union
			{
				uint8_t user_data[2336];

				struct
				{
					struct SubHeader
					{
						uint8_t file_number;
						uint8_t channel;
						uint8_t submode;
						uint8_t coding_info;
					} sub_header;
					SubHeader sub_header_copy;

					union
					{
						struct
						{
							uint8_t user_data[FORM1_DATA_SIZE];
							uint32_t edc;
							ECC ecc;
						} form1;
						struct
						{
							uint8_t user_data[FORM2_DATA_SIZE];
							uint32_t edc; // reserved
						} form2;
					};
				} xa;
			};
		} mode2;
	};
};

inline constexpr uint32_t CD_DATA_SIZE = 2352;
inline constexpr uint32_t CD_C2_SIZE = 294;
inline constexpr uint32_t CD_SUBCODE_SIZE = 96;
inline constexpr int32_t CD_SAMPLE_SIZE = sizeof(int16_t) * 2; // 16-bit signed sample, stereo

inline constexpr uint32_t CD_TRACKS_COUNT = 100;
inline constexpr uint32_t CD_INDEX_COUNT = 100;

inline constexpr uint8_t CD_DATA_SYNC[] =
{
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

inline constexpr uint32_t MSF_MINUTES_WRAP = 90;
inline constexpr MSF MSF_LIMIT = {100, 60, 75};
inline constexpr MSF MSF_ZERO = {0, 0, 0};
inline constexpr MSF MSF_MAX = {MSF_MINUTES_WRAP - 1, MSF_LIMIT.s, MSF_LIMIT.f};
inline constexpr int32_t MSF_LBA_SHIFT = -150;
inline constexpr MSF MSF_LEADIN_START = {MSF_MINUTES_WRAP, 0, 0};

inline constexpr uint32_t LBA_LIMIT = MSF_LIMIT.m * MSF_LIMIT.s * MSF_LIMIT.f;

template<typename T>
constexpr T bcd_decode(T value)
{
	return value / 0x10 * 10 + value % 0x10;
}


template<typename T>
constexpr T bcd_encode(T value)
{
	return value / 10 * 0x10 + value % 10;
}


MSF BCDMSF_to_MSF(MSF bcdmsf);
MSF MSF_to_BCDMSF(MSF msf);
int32_t MSF_to_LBA(MSF msf);
MSF LBA_to_MSF(int32_t lba);
int32_t BCDMSF_to_LBA(MSF bcdmsf);
MSF LBA_to_BCDMSF(int32_t lba);
bool MSF_valid(MSF msf);
bool BCDMSF_valid(MSF bcdmsf);

}
