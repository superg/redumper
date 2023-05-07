module;
#include <cstdint>
#include <format>
#include <map>
#include <string>

export module cd.subcode;

import cd;
import common;
import crc.crc16_gsm;
import endian;



namespace gpsxre
{

export enum class Subchannel : uint8_t
{
	W, V, U, T, S, R, Q, P
};


export struct ChannelQ
{
	enum class Control : uint8_t
	{
		PRE_EMPHASIS = 1 << 0,
		DIGITAL_COPY = 1 << 1,
		DATA         = 1 << 2,
		FOUR_CHANNEL = 1 << 3
	};

	union
	{
		struct
		{
			uint8_t adr     :4;
			uint8_t control :4;

			union
			{
				struct
				{
					uint8_t tno;
					uint8_t point_index;
					MSF msf;
					uint8_t zero;
					MSF a_msf;
				} mode1;

				struct
				{
					union
					{
						struct
						{
							uint8_t mcn[7];
							uint8_t zero;
						};

						uint8_t isrc[8];
					};

					uint8_t a_frame;
				} mode23;
			};
		};

		uint8_t raw[10];
	};

	uint16_t crc;


	bool isValid() const
	{
		return CRC16_GSM().update(raw, sizeof(raw)).final() == endian_swap(crc);
	}


	std::string Decode() const
	{
		std::string q_data;
		switch(adr)
		{
		case 1:
			q_data = std::format("tno: {:02X}, P/I: {:02X}, MSF: {:02X}:{:02X}:{:02X}, zero: {:02X}, A/P MSF: {:02X}:{:02X}:{:02X}",
								mode1.tno, mode1.point_index, mode1.msf.m, mode1.msf.s, mode1.msf.f, mode1.zero, mode1.a_msf.m, mode1.a_msf.s, mode1.a_msf.f);
			break;

			// RAW
		default:
			q_data = std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
								raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9]);
		}

		return std::format("control: {:04b}, ADR: {}, {}, crc: {:04X} ({})", control, adr, q_data, crc, isValid() ? "+" : "-");
	}


	ChannelQ generateMode1(int32_t shift) const
	{
		ChannelQ Q(*this);

		uint32_t value_limit = BCDMSF_to_LBA(Q.mode1.msf) - MSF_to_LBA(MSF_ZERO);
		uint32_t offset = std::abs(shift);

		if(shift > 0 && Q.mode1.point_index == 0 || shift < 0 && Q.mode1.point_index != 0)
		{
			if(offset > value_limit)
			{
				Q.mode1.point_index = !Q.mode1.point_index;
				Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(MSF_ZERO) + offset - value_limit);
			}
			else
			{
				if(offset == value_limit)
					Q.mode1.point_index = 1;
				Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.msf) - offset);
			}
		}
		else
			Q.mode1.msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.msf) + offset);

		Q.mode1.a_msf = LBA_to_BCDMSF(BCDMSF_to_LBA(Q.mode1.a_msf) + shift);
		Q.crc = endian_swap(CRC16_GSM().update(Q.raw, sizeof(Q.raw)).final());

		return Q;
	}


	ChannelQ generateMode23(const ChannelQ &base, int32_t shift) const
	{
		ChannelQ Q(base);

		Q.mode23.a_frame = (mode1.a_msf.f + shift) % MSF_LIMIT.f;
		Q.crc = endian_swap(CRC16_GSM().update(Q.raw, sizeof(Q.raw)).final());

		return Q;
	}
};


export void subcode_extract_channel(uint8_t *subchannel, const uint8_t *subcode, Subchannel name)
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


export bool subcode_correct_subq(ChannelQ *subq, uint32_t sectors_count)
{
	uint32_t mcn = sectors_count;
	std::map<uint8_t, uint32_t> isrc;
	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	bool invalid_subq = true;
	uint8_t tno = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!subq[lba_index].isValid())
			continue;

		invalid_subq = false;

		if(subq[lba_index].adr == 1)
			tno = subq[lba_index].mode1.tno;
		else if(subq[lba_index].adr == 2 && mcn == sectors_count)
			mcn = lba_index;
		else if(subq[lba_index].adr == 3 && tno && isrc.find(tno) == isrc.end())
			isrc[tno] = lba_index;
	}

	if(invalid_subq)
		return false;

	uint32_t q_prev = sectors_count;
	uint32_t q_next = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!memcmp(&subq[lba_index], &q_empty, sizeof(q_empty)))
			continue;

		if(subq[lba_index].isValid())
		{
			if(subq[lba_index].adr == 1)
			{
				if(subq[lba_index].mode1.tno)
					q_prev = lba_index;
				else
					q_prev = sectors_count;
			}
		}
		else
		{
			// find next valid Q
			if(lba_index >= q_next && q_next != sectors_count)
			{
				q_next = lba_index + 1;
				for(; q_next < sectors_count; ++q_next)
					if(subq[q_next].isValid())
					{
						if(subq[q_next].adr == 1)
						{
							if(!subq[q_next].mode1.tno)
								q_next = 0;

							break;
						}
					}
			}

			std::vector<ChannelQ> candidates;
			if(q_prev < lba_index)
			{
				// mode 1
				candidates.emplace_back(subq[q_prev].generateMode1(lba_index - q_prev));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subq[q_prev].generateMode23(subq[mcn], lba_index - q_prev));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_prev].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subq[q_prev].generateMode23(subq[it->second], lba_index - q_prev));
				}
			}

			if(q_next > lba_index && q_next != sectors_count)
			{
				// mode 1
				candidates.emplace_back(subq[q_next].generateMode1(lba_index - q_next));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subq[q_next].generateMode23(subq[mcn], lba_index - q_next));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_next].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subq[q_next].generateMode23(subq[it->second], lba_index - q_next));
				}
			}

			if(!candidates.empty())
			{
				uint32_t c = 0;
				for(uint32_t j = 0; j < (uint32_t)candidates.size(); ++j)
					if(bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[j], sizeof(ChannelQ) / sizeof(uint32_t)) < bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[c], sizeof(ChannelQ) / sizeof(uint32_t)))
						c = j;

				subq[lba_index] = candidates[c];
			}
		}
	}

	return true;
}

}
