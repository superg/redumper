module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "throw_line.hh"

export module utils.xbox;

import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.misc;



namespace gpsxre
{

#pragma pack(push, 1)
export union XGD_SecuritySector
{
    struct Challenge
    {
        uint8_t valid;
        uint8_t challenge_id;
        uint32_t challenge_value;
        uint8_t response_modifier;
        uint32_t response_value;
    };

    struct Range
    {
        uint8_t unknown[3];
        uint8_t psn_start[3];
        uint8_t psn_end[3];
    };

    READ_DVD_STRUCTURE_LayerDescriptor ld;
    struct
    {
        uint8_t layer_descriptor[17]; // 0x000    0
        uint8_t unknown_011[703];     // 0x011   17
        uint32_t cpr_mai;             // 0x2D0  720
        uint8_t unknown_2D4[44];      // 0x2D4  724
        uint8_t challenge_version;    // 0x300  768
        uint8_t challenge_count;      // 0x301  769
        uint8_t challenges[285];      // 0x302  770
        uint64_t creation_filetime;   // 0x41F 1055
        uint8_t unknown_427[16];      // 0x427 1063
        uint32_t unknown_437;         // 0x437 1079
        uint8_t unknown_43B[16];      // 0x43B 1083
        uint8_t unknown_44B[84];      // 0x44B 1099
        uint64_t authoring_filetime;  // 0x49F 1183
        uint32_t cert_unixtime;       // 0x4A7 1191
        uint8_t unknown_4AB[15];      // 0x4AB 1195
        uint8_t type_a;               // 0x4BA 1210
        uint8_t game_id[16];          // 0x4BB 1211
        uint8_t sha1_a[20];           // 0x4CB 1227
        uint8_t signature_a[256];     // 0x4DF 1247
        uint64_t mastering_filetime;  // 0x5DF 1503
        uint8_t unknown_5E7[19];      // 0x5E7 1511
        uint8_t type_b;               // 0x5FA 1530
        uint8_t factory_id[16];       // 0x5FB 1531
        uint8_t sha1_b[20];           // 0x60B 1547
        uint8_t signature_b[64];      // 0x61F 1567
        uint8_t range_version;        // 0x65F 1631
        uint8_t range_count;          // 0x660 1632
        Range ranges[23];             // 0x661 1633
        Range ranges_copy[23];        // 0x730 1840
        uint8_t unknown_7FF;          // 0x7FF 2047
    };
};
#pragma pack(pop)

export const uint32_t XGD_SS_LEADOUT_SECTOR = 4267582;

export enum class XGD_Type : uint8_t
{
    UNKNOWN,
    XGD1,
    XGD2,
    XGD3
};

export struct XGD2_SecuritySector
{
    uint8_t reserved1[239];
    uint64_t unknown1;
    uint8_t sha1X[20];
    uint8_t reserved2[228];
    uint8_t cr_data[207];
    uint8_t reserved3;
    uint32_t cpr_mai;
    uint8_t reserved4[44];
    uint8_t ccrt_version;
    uint8_t ccrt_length;
    uint8_t reserved5[2];
    uint8_t ccrt[252];
    uint8_t reserved6[96];
    uint8_t media_id[16];
    uint8_t reserved7[46];
    uint8_t unknown2;
    uint64_t authoring_filetime;
    uint8_t reserved8[19];
    uint8_t typeA;
    uint8_t game_id[16];
    uint8_t sha1A[20];
    uint8_t signatureA[256];
    uint64_t mastering_filetime;
    uint8_t reserved9[19];
    uint8_t typeB;
    uint8_t factory_id[16];
    uint8_t sha1B[20];
    uint8_t signatureB[64];
    uint8_t ss_version;
    uint8_t ranges_length;
    uint8_t security_ranges[207];
    uint8_t security_ranges_duplicate[207];
    uint8_t reserved10;
};

export XGD_Type get_xgd_type(const READ_DVD_STRUCTURE_LayerDescriptor &ss_layer_descriptor)
{
    const uint32_t xgd_type = endian_swap<uint32_t>(ss_layer_descriptor.layer0_end_sector);

    // Return XGD type based on value
    switch(xgd_type)
    {
    case 0x2033AF:
        return XGD_Type::XGD1;
    case 0x20339F:
        return XGD_Type::XGD2;
    case 0x238E0F:
        return XGD_Type::XGD3;
    default:
        return XGD_Type::UNKNOWN;
    }
}

export std::vector<std::pair<uint32_t, uint32_t>> get_security_sector_ranges(const READ_DVD_STRUCTURE_LayerDescriptor &ss_layer_descriptor)
{
    std::vector<std::pair<uint32_t, uint32_t>> ss_ranges;

    const auto media_specific_offset = offsetof(READ_DVD_STRUCTURE_LayerDescriptor, media_specific);
    uint8_t num_ss_regions = ss_layer_descriptor.media_specific[1632 - media_specific_offset];
    bool is_xgd1 = (get_xgd_type(ss_layer_descriptor) == XGD_Type::XGD1);
    // partial pre-compute of conversion to Layer 1
    int32_t ss_layer0_last = sign_extend<24>(endian_swap(ss_layer_descriptor.layer0_end_sector));
    const uint32_t layer1_offset = (ss_layer0_last * 2) - 0x30000 + 1;

    for(int ss_pos = 1633 - media_specific_offset, i = 0; i < num_ss_regions; ss_pos += 9, ++i)
    {
        uint32_t start_psn = ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 3] << 16) | ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 4] << 8)
                           | (uint32_t)ss_layer_descriptor.media_specific[ss_pos + 5];
        uint32_t end_psn = ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 6] << 16) | ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 7] << 8)
                         | (uint32_t)ss_layer_descriptor.media_specific[ss_pos + 8];
        if((i < 8 && is_xgd1) || (i == 0 && !is_xgd1))
        {
            // Layer 0
            ss_ranges.push_back({ start_psn - 0x30000, end_psn - 0x30000 });
        }
        else if((i < 16 && is_xgd1) || (i == 3 && !is_xgd1))
        {
            // Layer 1
            ss_ranges.push_back({ layer1_offset - (start_psn ^ 0xFFFFFF), layer1_offset - (end_psn ^ 0xFFFFFF) });
        }
    }

    return ss_ranges;
}

export void clean_xbox_security_sector(std::vector<uint8_t> &security_sector)
{
    XGD_Type xgd_type = get_xgd_type((READ_DVD_STRUCTURE_LayerDescriptor &)security_sector[0]);

    bool ssv2 = false;
    switch(xgd_type)
    {
    case XGD_Type::XGD1:
        // no fix needed
        break;

    case XGD_Type::XGD2:
        security_sector[552] = 0x01;
        security_sector[553] = 0x00;
        security_sector[555] = 0x00;
        security_sector[556] = 0x00;

        security_sector[561] = 0x5B;
        security_sector[562] = 0x00;
        security_sector[564] = 0x00;
        security_sector[565] = 0x00;

        security_sector[570] = 0xB5;
        security_sector[571] = 0x00;
        security_sector[573] = 0x00;
        security_sector[574] = 0x00;

        security_sector[579] = 0x0F;
        security_sector[580] = 0x01;
        security_sector[582] = 0x00;
        security_sector[583] = 0x00;
        break;

    case XGD_Type::XGD3:
        // determine if ssv1 (Kreon) or ssv2 (0800)
        ssv2 = std::any_of(security_sector.begin() + 32, security_sector.begin() + 32 + 72, [](uint8_t x) { return x != 0; });

        if(ssv2)
        {
            security_sector[72] = 0x01;
            security_sector[73] = 0x00;
            security_sector[75] = 0x01;
            security_sector[76] = 0x00;

            security_sector[81] = 0x5B;
            security_sector[82] = 0x00;
            security_sector[84] = 0x5B;
            security_sector[85] = 0x00;

            security_sector[90] = 0xB5;
            security_sector[91] = 0x00;
            security_sector[93] = 0xB5;
            security_sector[94] = 0x00;

            security_sector[99] = 0x0F;
            security_sector[100] = 0x01;
            security_sector[102] = 0x0F;
            security_sector[103] = 0x01;
        }
        else
        {
            security_sector[552] = 0x01;
            security_sector[553] = 0x00;

            security_sector[561] = 0x5B;
            security_sector[562] = 0x00;

            security_sector[570] = 0xB5;
            security_sector[571] = 0x00;

            security_sector[579] = 0x0F;
            security_sector[580] = 0x01;
        }
        break;

    case XGD_Type::UNKNOWN:
    default:
        // cannot clean
        break;
    }
}

export bool xbox_get_security_sector(SPTD &sptd, std::vector<uint8_t> &response_data, bool kreon_partial_ss)
{
    const uint8_t ss_vals[4] = { 0x01, 0x03, 0x05, 0x07 };

    std::size_t i = 0;
    for(; i < (kreon_partial_ss ? 1 : sizeof(ss_vals)); ++i)
    {
        auto status = cmd_kreon_get_security_sector(sptd, response_data, ss_vals[i]);
        if(status.status_code)
        {
            // fail if cannot get initial response, otherwise just note partial response
            if(i == 0)
                throw_line("failed to get security sector, SCSI ({})", SPTD::StatusMessage(status));

            break;
        }
    }

    return i == sizeof(ss_vals);
}

export bool xbox_repair_xgd3_ss(std::vector<uint8_t> &security_sector, std::vector<uint8_t> &ss_leadout)
{
    // simple sanity check that leadout sector is a valid SS to repair from
    if(!std::equal(security_sector.begin(), security_sector.begin() + 32, ss_leadout.begin()))
        return false;

    // move incorrectly placed challenge data and cpr.mai key
    std::memcpy(&security_sector[0x020], &security_sector[0x200], 8);
    std::memcpy(&security_sector[0x029], &security_sector[0x209], 8);
    std::memcpy(&security_sector[0x032], &security_sector[0x212], 8);
    std::memcpy(&security_sector[0x03B], &security_sector[0x21B], 8);
    std::memcpy(&security_sector[0x044], &security_sector[0x224], 6);
    std::memcpy(&security_sector[0x04D], &security_sector[0x22D], 6);
    std::memcpy(&security_sector[0x056], &security_sector[0x236], 6);
    std::memcpy(&security_sector[0x05F], &security_sector[0x23F], 6);
    std::memcpy(&security_sector[0x0F0], &security_sector[0x2D0], 4);

    // repair using correct values from leadout sector
    std::memcpy(&security_sector[0x200], &ss_leadout[0x200], 8);
    std::memcpy(&security_sector[0x209], &ss_leadout[0x209], 8);
    std::memcpy(&security_sector[0x212], &ss_leadout[0x212], 8);
    std::memcpy(&security_sector[0x21B], &ss_leadout[0x21B], 8);
    std::memcpy(&security_sector[0x224], &ss_leadout[0x224], 6);
    std::memcpy(&security_sector[0x22D], &ss_leadout[0x22D], 6);
    std::memcpy(&security_sector[0x236], &ss_leadout[0x236], 6);
    std::memcpy(&security_sector[0x23F], &ss_leadout[0x23F], 6);
    std::memcpy(&security_sector[0x2D0], &ss_leadout[0x2D0], 4);

    return true;
}

}
