module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "throw_line.hh"

export module utils.xbox;

import range;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.misc;



namespace gpsxre::xbox
{

export struct Context
{
    std::vector<Range<uint32_t>> skip_ranges;
    uint32_t lock_sector;
    uint32_t l1_video_shift;
};


#pragma pack(push, 1)
export union SecurityLayerDescriptor
{
    struct Range
    {
        uint8_t unknown[3];
        uint8_t psn_start[3];
        uint8_t psn_end[3];
    };

    struct ChallengeResponse
    {
        uint8_t unknown[9];
    };

    READ_DVD_STRUCTURE_LayerDescriptor ld;
    struct
    {
        uint8_t ld_raw[17]; // 0x000    0

        union
        {
            struct
            {
                uint8_t reserved_011[703];  // 0x011   17
                uint32_t cpr_mai;           // 0x2D0  720
                uint8_t reserved_2D4[44];   // 0x2D4  724
                uint8_t ccrt_version;       // 0x300  768
                uint8_t ccrt_count;         // 0x301  769
                uint8_t ccrt[253];          // 0x302  770
                uint8_t reserved_3FF[32];   // 0x3FF 1023
                uint64_t creation_filetime; // 0x41F 1055
                uint8_t unknown_427[16];    // 0x427 1063
                uint32_t reserved_437;      // 0x437 1079
                uint8_t unknown_43B[16];    // 0x43B 1083
                uint8_t reserved_44B[84];   // 0x44B 1099
            } xgd1;

            struct
            {
                union
                {
                    struct
                    {
                        uint8_t reserved_011[239]; // 0x011   17
                        uint32_t unknown_100;      // 0x100  256
                        uint32_t unknown_104;      // 0x104  260
                        uint8_t unknown_108[20];   // 0x108  264
                        uint8_t reserved_11C[228]; // 0x11C  284
                        ChallengeResponse crd[23]; // 0x200  512
                        uint8_t reserved_2CF;      // 0x2CF  719
                        uint32_t cpr_mai;          // 0x2D0  720
                        uint8_t reserved_2D4[44];  // 0x2D4  724
                    } xgd2;

                    struct
                    {
                        uint8_t reserved_011[10];  // 0x011   17
                        uint8_t unknown_1B;        // 0x01B   27
                        uint8_t reserved_1C[4];    // 0x01C   28
                        ChallengeResponse crd[23]; // 0x020   32
                        uint8_t reserved_0EF;      // 0x0EF  239
                        uint32_t cpr_mai;          // 0x0F0  240
                        uint8_t reserved_0F4[12];  // 0x0F4  244
                        uint32_t unknown_100;      // 0x100  257
                        uint32_t unknown_104;      // 0x104  261
                        uint8_t unknown_108[504];  // 0x108  265
                    } xgd3;
                };

                uint8_t ccrt_version;     // 0x300  768
                uint8_t ccrt_count;       // 0x301  769
                uint8_t reserved_302[2];  // 0x302  770
                uint8_t ccrt[252];        // 0x304  772
                uint8_t reserved_400[96]; // 0x400 1024
                uint8_t media_id[16];     // 0x460 1120
                uint8_t reserved_470[46]; // 0x470 1136
                uint8_t unknown_49E;      // 0x49E 1182
            } xgd23;
        };

        uint64_t authoring_filetime; // 0x49F 1183
        uint32_t cert_unixtime;      // 0x4A7 1191
        uint8_t reserved_4AB[15];    // 0x4AB 1195
        uint8_t unknown_4BA;         // 0x4BA 1210
        uint8_t game_id[16];         // 0x4BB 1211
        uint8_t sha1_a[20];          // 0x4CB 1227
        uint8_t signature_a[256];    // 0x4DF 1247
        uint64_t mastering_filetime; // 0x5DF 1503
        uint8_t reserved_5E7[4];     // 0x5E7 1511
        uint8_t reserved_5EB[15];    // 0x5EB 1515
        uint8_t unknown_5FA;         // 0x5FA 1530
        uint8_t factory_id[16];      // 0x5FB 1531
        uint8_t sha1_b[20];          // 0x60B 1547
        uint8_t signature_b[64];     // 0x61F 1567
        uint8_t version;             // 0x65F 1631
        uint8_t range_count;         // 0x660 1632
        Range ranges[23];            // 0x661 1633
        Range ranges_copy[23];       // 0x730 1840
        uint8_t reserved_7FF;        // 0x7FF 2047
    };
};
#pragma pack(pop)


// TODO: maybe map?
export constexpr int32_t XGD1_L0_LAST = 2110383;
export constexpr int32_t XGD2_L0_LAST = 2110367;
export constexpr int32_t XGD3_L0_LAST = 2330127;
export constexpr uint32_t XGD_SS_LEADOUT_SECTOR = 4267582;


export bool read_security_layer_descriptor(SPTD &sptd, std::vector<uint8_t> &response_data, bool kreon_partial_ss)
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


uint32_t PSN_to_LBA(int32_t psn, int32_t layer0_last)
{
    psn -= 0x30000;
    if(psn < 0)
        psn += (layer0_last + 1) * 2;

    return psn;
}


export void get_security_layer_descriptor_ranges(std::vector<Range<uint32_t>> &skip_ranges, const SecurityLayerDescriptor &sld)
{
    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    for(uint32_t i = 0; i < (uint32_t)sld.range_count; ++i)
    {
        if(layer0_last == XGD1_L0_LAST && i >= 16 || layer0_last != XGD1_L0_LAST && i != 0 && i != 3)
            continue;

        auto psn_start = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_start));
        auto psn_end = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_end));

        if(!insert_range(skip_ranges, { PSN_to_LBA(psn_start, layer0_last), PSN_to_LBA(psn_end, layer0_last) + 1 }))
            throw_line("invalid range configuration");
    }
}


export void clean_security_layer_descriptor(SecurityLayerDescriptor &sld)
{
    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    constexpr uint16_t DEFAULT_VALUES[] = { 0x01, 0x5B, 0xB5, 0x10F };

    if(layer0_last == XGD2_L0_LAST)
    {
        for(std::size_t i = 0; i < std::size(DEFAULT_VALUES); ++i)
        {
            (uint16_t &)sld.xgd23.xgd2.crd[4 + i].unknown[4] = DEFAULT_VALUES[i];
            (uint16_t &)sld.xgd23.xgd2.crd[4 + i].unknown[7] = 0;
        }
    }
    else if(layer0_last == XGD3_L0_LAST)
    {
        for(std::size_t i = 0; i < std::size(DEFAULT_VALUES); ++i)
        {
            (uint16_t &)sld.xgd23.xgd3.crd[4 + i].unknown[4] = DEFAULT_VALUES[i];
            (uint16_t &)sld.xgd23.xgd3.crd[4 + i].unknown[7] = DEFAULT_VALUES[i];
        }
    }
}


export bool merge_xgd3_security_layer_descriptor(std::vector<uint8_t> &security_sector, const std::vector<uint8_t> &ss_leadout)
{
    std::vector<uint8_t> security_sector_copy = security_sector;
    security_sector = ss_leadout;

    SecurityLayerDescriptor &sld = (SecurityLayerDescriptor &)security_sector[0];
    SecurityLayerDescriptor &sld_kreon = (SecurityLayerDescriptor &)security_sector_copy[0];

    memcpy(sld.ranges, sld_kreon.ranges, sizeof(sld.ranges));
    memcpy(sld.ranges_copy, sld_kreon.ranges_copy, sizeof(sld.ranges_copy));
    sld.xgd23.xgd3.cpr_mai = sld_kreon.xgd23.xgd2.cpr_mai;

    for(uint32_t i = 0; i < 4; ++i)
    {
        memcpy(sld.xgd23.xgd3.crd[i].unknown, sld_kreon.xgd23.xgd2.crd[i].unknown, 8);
        memcpy(sld.xgd23.xgd3.crd[4 + i].unknown, sld_kreon.xgd23.xgd2.crd[4 + i].unknown, 6);
    }

    return true;
}

}
