module;
#include <climits>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

export module cd.subcode;

import cd.cd;
import crc.crc16_gsm;
import utils.endian;
import utils.misc;



namespace gpsxre
{

export enum class Subchannel : uint8_t
{
    W,
    V,
    U,
    T,
    S,
    R,
    Q,
    P
};


export struct ChannelP
{
    uint8_t pause[12];
};


export struct ChannelQ
{
    enum class Control : uint8_t
    {
        PRE_EMPHASIS = 1 << 0,
        DIGITAL_COPY = 1 << 1,
        DATA = 1 << 2,
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


    bool isValid(int32_t lba)
    {
        bool valid = isValid();

        if(valid && adr == 1 && mode1.tno && lba != BCDMSF_to_LBA(mode1.a_msf))
            valid = false;

        return valid;
    }

    std::string Decode() const
    {
        std::string q_data;
        switch(adr)
        {
        case 1:
            q_data = std::format("tno: {:02X}, P/I: {:02X}, MSF: {:02X}:{:02X}:{:02X}, zero: {:02X}, A/P MSF: {:02X}:{:02X}:{:02X}", mode1.tno, mode1.point_index, mode1.msf.m, mode1.msf.s,
                mode1.msf.f, mode1.zero, mode1.a_msf.m, mode1.a_msf.s, mode1.a_msf.f);
            break;

            // RAW
        default:
            q_data = std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9]);
        }

        return std::format("control: {:04b}, ADR: {}, {}, crc: {:04X} ({})", control, adr, q_data, crc, isValid() ? "+" : "-");
    }


    ChannelQ generateMode1(int32_t shift) const
    {
        ChannelQ Q(*this);

        uint32_t value_limit = BCDMSF_to_LBA(Q.mode1.msf) - MSF_to_LBA(MSF_ZERO);
        uint32_t offset = std::abs(shift);

        if((shift > 0 && Q.mode1.point_index == 0) || (shift < 0 && Q.mode1.point_index != 0))
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


export ChannelQ subcode_extract_q(const uint8_t *subcode)
{
    ChannelQ Q;
    subcode_extract_channel((uint8_t *)&Q, subcode, Subchannel::Q);

    return Q;
}


// Decodes ISRC from Q subchannel data
// Parameters:
//   isrc_bytes - Must point to at least 8 bytes of ISRC data from Q subchannel
// Returns: Decoded ISRC string (12 characters), or empty string if invalid
export std::string decode_isrc(const uint8_t *isrc_bytes)
{
    if(!isrc_bytes)
        return {};

    // ISRC format: 5 characters (6 bits each) + 7 BCD digits
    // Total: 12 characters (CC-OOO-YY-NNNNN)

    constexpr char ISRC_TABLE[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_', '_', '_', '_', '_', '_', '_', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_' };

    std::string result;

    // Extract 5 letters (6 bits each)
    for(uint32_t i = 0; i < 5; ++i)
    {
        uint8_t c = 0;
        bit_copy(&c, 2, isrc_bytes, i * 6, 6);
        char ch = ISRC_TABLE[c];
        if(ch == '_')
            return {}; // Invalid character, return empty string
        result += ch;
    }

    // 2 bits padding

    // Extract 7 BCD digits (from bytes 4-7, each byte contains 2 BCD digits)
    for(uint32_t i = 4; i < 8; ++i)
        result += std::format("{:02}", bcd_decode(isrc_bytes[i]));

    // Remove trailing digit (ISRC standard is 12 chars, not 13)
    result.pop_back();

    return result;
}


// Decodes MCN (Media Catalog Number) from Q subchannel data
// Parameters:
//   mcn_bytes - Must point to at least 7 bytes of MCN data from Q subchannel
// Returns: Decoded MCN string (13 digits), or empty string if invalid
export std::string decode_mcn(const uint8_t *mcn_bytes)
{
    if(!mcn_bytes)
        return {};

    // MCN format: 13 BCD digits stored in 7 bytes
    std::string result;

    for(uint32_t i = 0; i < 7; ++i)
        result += std::format("{:02}", bcd_decode(mcn_bytes[i]));

    // Remove trailing digit (MCN is 13 digits, 7 bytes = 14 BCD digits)
    result.pop_back();

    return result;
}

}
