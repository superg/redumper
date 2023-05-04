module;
#include <cstdint>
#include <cstring>

export module cd;



namespace gpsxre
{

export struct MSF
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

export constexpr uint32_t FORM1_DATA_SIZE = 2048;
export constexpr uint32_t FORM2_DATA_SIZE = 2324;
export constexpr uint32_t MODE0_DATA_SIZE = 2336;

export enum class CDXAMode : uint8_t
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

export struct Sector
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
				uint8_t user_data[MODE0_DATA_SIZE];

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

export constexpr uint32_t CD_DATA_SIZE = 2352;
export constexpr uint32_t CD_C2_SIZE = 294;
export constexpr uint32_t CD_SUBCODE_SIZE = 96;
export constexpr uint32_t CD_RAW_DATA_SIZE = CD_DATA_SIZE + CD_C2_SIZE + CD_SUBCODE_SIZE;
export constexpr int32_t CD_SAMPLE_SIZE = sizeof(int16_t) * 2; // 16-bit signed sample, stereo
export constexpr uint32_t CD_DATA_SIZE_SAMPLES = CD_DATA_SIZE / CD_SAMPLE_SIZE;

export constexpr uint32_t CD_TRACKS_COUNT = 100;
export constexpr uint32_t CD_INDEX_COUNT = 100;
export constexpr uint32_t CD_LEADOUT_TRACK_NUMBER = 0xAA;

export constexpr uint8_t CD_DATA_SYNC[] =
{
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

export constexpr uint32_t MSF_MINUTES_WRAP = 90;
export constexpr MSF MSF_ZERO = {0, 0, 0};
export constexpr MSF MSF_LIMIT = {100, 60, 75};
export constexpr MSF MSF_MAX = {MSF_MINUTES_WRAP - 1, MSF_LIMIT.s, MSF_LIMIT.f};

export constexpr MSF MSF_LEADIN_START = {MSF_MINUTES_WRAP, 0, 0};

export constexpr uint32_t LBA_LIMIT = MSF_LIMIT.m * MSF_LIMIT.s * MSF_LIMIT.f;

export constexpr uint32_t CD_PREGAP_SIZE = 2 * MSF_LIMIT.f; // 2 seconds
export constexpr uint32_t CD_LEADIN_MIN_SIZE = 60 * MSF_LIMIT.f; // 60 seconds
export constexpr uint32_t CD_LEADOUT_MIN_SIZE = 90 * MSF_LIMIT.f; // 90 seconds

export constexpr int32_t MSF_LBA_SHIFT = -1 * CD_PREGAP_SIZE;

export template<typename T>
constexpr T bcd_decode(T value)
{
	return value / 0x10 * 10 + value % 0x10;
}


export template<typename T>
constexpr T bcd_encode(T value)
{
	return value / 10 * 0x10 + value % 10;
}

export MSF BCDMSF_to_MSF(MSF bcdmsf)
{
	MSF msf;
	msf.m = bcd_decode(bcdmsf.m);
	msf.s = bcd_decode(bcdmsf.s);
	msf.f = bcd_decode(bcdmsf.f);

	return msf;
}


export MSF MSF_to_BCDMSF(MSF msf)
{
	MSF bcdmsf;
	bcdmsf.m = bcd_encode(msf.m);
	bcdmsf.s = bcd_encode(msf.s);
	bcdmsf.f = bcd_encode(msf.f);

	return bcdmsf;
}


export int32_t MSF_to_LBA(MSF msf)
{
	return MSF_LIMIT.f * (MSF_LIMIT.s * msf.m + msf.s) + msf.f + MSF_LBA_SHIFT - (msf.m >= MSF_MINUTES_WRAP ? LBA_LIMIT : 0);
}


export MSF LBA_to_MSF(int32_t lba)
{
	MSF msf;

	lba -= MSF_LBA_SHIFT;

	if(lba < 0)
		lba += LBA_LIMIT;

	msf.f = lba % MSF_LIMIT.f;
	lba /= MSF_LIMIT.f;
	msf.s = lba % MSF_LIMIT.s;
	lba /= MSF_LIMIT.s;
	msf.m = lba;

	return msf;
}


export int32_t BCDMSF_to_LBA(MSF bcdmsf)
{
	return MSF_to_LBA(BCDMSF_to_MSF(bcdmsf));
}


export MSF LBA_to_BCDMSF(int32_t lba)
{
	return MSF_to_BCDMSF(LBA_to_MSF(lba));
}


export bool MSF_valid(MSF msf)
{
	return msf.m < MSF_LIMIT.m && msf.s < MSF_LIMIT.s && msf.f < MSF_LIMIT.f;
}


export bool BCDMSF_valid(MSF bcdmsf)
{
	return MSF_valid(BCDMSF_to_MSF(bcdmsf));
}

}
