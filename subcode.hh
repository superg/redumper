#pragma once



#include <cstdint>
#include <string>
#include "cd.hh"



namespace gpsxre
{

enum class Subchannel : uint8_t
{
	P = 7,
	Q = 6,
	R = 5,
	S = 4,
	T = 3,
	U = 2,
	V = 1,
	W = 0
};

struct ChannelQ
{
	enum class Control : uint8_t
	{
		FOUR_CHANNEL = 1 << 3,
		DATA         = 1 << 2,
		DIGITAL_COPY = 1 << 1,
		PRE_EMPHASIS = 1 << 0
	};

	union
	{
		struct
		{
			uint8_t control_adr;

			union
			{
//				uint8_t data[9];
				struct Mode1
				{
					union
					{
						// lead-in
						struct
						{
							uint8_t tno;
							uint8_t point;
							MSF msf;
							uint8_t zero;
							MSF p_msf;
						} leadin;

						// track
						struct
						{
							uint8_t tno;
							uint8_t index;
							MSF msf;
							uint8_t zero;
							MSF a_msf;
						};
					};
				} mode1;

				struct Mode2
				{
					uint8_t mcn[7];
					uint8_t zero;
					uint8_t a_frame;
				} mode2;

				struct Mode3
				{
					uint8_t isrc[8];
					uint8_t a_frame;
				} mode3;
			};
		};

		uint8_t raw[10];
	};

	uint16_t crc;

	bool Valid() const;
	std::string Decode() const;
};

void subcode_extract_channel(uint8_t *subchannel, uint8_t *subcode, Subchannel name);
ChannelQ subchannel_q_generate_mode1(const ChannelQ &base, int32_t shift);
ChannelQ subchannel_q_generate_mode2(const ChannelQ &base, const ChannelQ &mode1, int32_t shift);
ChannelQ subchannel_q_generate_mode3(const ChannelQ &base, const ChannelQ &mode1, int32_t shift);

}
