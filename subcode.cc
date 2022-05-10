#include <climits>
#include <format>
#include "cd.hh"
#include "crc16_gsm.hh"
#include "endian.hh"
#include "subcode.hh"



namespace gpsxre
{

bool ChannelQ::Valid() const
{
	return crc16_gsm(raw, sizeof(raw)) == endian_swap(crc);
}


std::string ChannelQ::Decode() const
{
	std::string q_data;
	uint8_t control = control_adr >> 4;
	uint8_t adr = control_adr & 0x0F;
	switch(adr)
	{
	case 1:
		q_data = std::format("tno: {:02X}, P/I: {:02X}, MSF: {:02X}:{:02X}:{:02X}, zero: {:02X}, A/P MSF: {:02X}:{:02X}:{:02X}",
							 mode1.tno, mode1.index, mode1.msf.m, mode1.msf.s, mode1.msf.f, mode1.zero, mode1.a_msf.m, mode1.a_msf.s, mode1.a_msf.f);
		break;

		// RAW
	default:
		q_data = std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
							 raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9]);
	}

	return std::format("control: {:04b}, ADR: {}, {}, crc: {:04X} ({})", control, adr, q_data, crc, Valid() ? "+" : "-");
}


void subcode_extract_channel(uint8_t *subchannel, uint8_t *subcode, Subchannel name)
{
	for(uint32_t i = 0; i < CD_SUBCODE_SIZE; ++i)
	{
		uint8_t &sc = subchannel[i / CHAR_BIT];
		uint8_t mask = 1 << (CHAR_BIT - 1 - i % 8);
		if(subcode[i] & (1 << (uint8_t)name))
			sc |= mask;
		else
			sc &= ~mask;
	}
}


ChannelQ subchannel_q_generate_mode1(const ChannelQ &base, int32_t shift)
{
	ChannelQ Q(base);

	uint32_t value_limit = BCDMSF_to_LBA(Q.mode1.msf) - MSF_to_LBA(MSF_ZERO);
	uint32_t offset = std::abs(shift);

	if(shift > 0 && Q.mode1.index == 0 || shift < 0 && Q.mode1.index != 0)
	{
		if(offset > value_limit)
		{
			Q.mode1.index = !Q.mode1.index;
			Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(MSF_ZERO) + offset - value_limit);
		}
		else
		{
			if(offset == value_limit)
				Q.mode1.index = 1;
			Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.msf) - offset);
		}
	}
	else
		Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.msf) + offset);

	Q.mode1.a_msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.a_msf) + shift);
	Q.crc = endian_swap(crc16_gsm(Q.raw, sizeof(Q.raw)));

	return Q;
}


ChannelQ subchannel_q_generate_mode2(const ChannelQ &base, const ChannelQ &mode1, int32_t shift)
{
	ChannelQ Q(base);

	Q.mode2.a_frame = (mode1.mode1.a_msf.f + shift) % MSF_LIMIT.f;
	Q.crc = endian_swap(crc16_gsm(Q.raw, sizeof(Q.raw)));

	return Q;
}


ChannelQ subchannel_q_generate_mode3(const ChannelQ &base, const ChannelQ &mode1, int32_t shift)
{
	ChannelQ Q(base);

	Q.mode3.a_frame = (mode1.mode1.a_msf.f + shift) % MSF_LIMIT.f;
	Q.crc = endian_swap(crc16_gsm(Q.raw, sizeof(Q.raw)));

	return Q;
}

}
