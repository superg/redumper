module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include "throw_line.hh"

export module dvd.xbox;

import cd.cdrom;
import range;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.logger;
import utils.misc;



namespace gpsxre::xbox
{

export struct Context
{
    std::vector<uint8_t> security_sector;
    uint32_t lock_lba_start;
    uint32_t layer1_video_lba_start;
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
                uint8_t reserved_011[703];  // 0x011
                uint32_t cpr_mai;           // 0x2D0
                uint8_t reserved_2D4[44];   // 0x2D4
                uint8_t ccrt_version;       // 0x300
                uint8_t ccrt_count;         // 0x301
                uint8_t ccrt[253];          // 0x302
                uint8_t reserved_3FF[32];   // 0x3FF
                uint64_t creation_filetime; // 0x41F
                uint8_t unknown_427[16];    // 0x427
                uint32_t reserved_437;      // 0x437
                uint8_t unknown_43B[16];    // 0x43B
                uint8_t reserved_44B[84];   // 0x44B
            } xgd1;

            struct
            {
                union
                {
                    struct
                    {
                        uint8_t reserved_011[239]; // 0x011
                        uint32_t unknown_100;      // 0x100
                        uint32_t unknown_104;      // 0x104
                        uint8_t unknown_108[20];   // 0x108
                        uint8_t reserved_11C[228]; // 0x11C
                        ChallengeResponse crd[23]; // 0x200
                        uint8_t reserved_2CF;      // 0x2CF
                        uint32_t cpr_mai;          // 0x2D0
                        uint8_t reserved_2D4[44];  // 0x2D4
                    } xgd2;

                    struct
                    {
                        uint8_t reserved_011[10];  // 0x011
                        uint8_t unknown_1B;        // 0x01B
                        uint8_t reserved_1C[4];    // 0x01C
                        ChallengeResponse crd[23]; // 0x020
                        uint8_t reserved_0EF;      // 0x0EF
                        uint32_t cpr_mai;          // 0x0F0
                        uint8_t reserved_0F4[12];  // 0x0F4
                        uint32_t unknown_100;      // 0x100
                        uint32_t unknown_104;      // 0x104
                        uint8_t unknown_108[504];  // 0x108
                    } xgd3;
                };

                uint8_t ccrt_version;     // 0x300
                uint8_t ccrt_count;       // 0x301
                uint8_t reserved_302[2];  // 0x302
                uint8_t ccrt[252];        // 0x304
                uint8_t reserved_400[96]; // 0x400
                uint8_t media_id[16];     // 0x460
                uint8_t reserved_470[46]; // 0x470
                uint8_t unknown_49E;      // 0x49E
            } xgd23;
        };

        uint64_t authoring_filetime; // 0x49F
        uint32_t cert_unixtime;      // 0x4A7
        uint8_t reserved_4AB[15];    // 0x4AB
        uint8_t unknown_4BA;         // 0x4BA
        uint8_t game_id[16];         // 0x4BB
        uint8_t sha1_a[20];          // 0x4CB
        uint8_t signature_a[256];    // 0x4DF
        uint64_t mastering_filetime; // 0x5DF
        uint8_t reserved_5E7[4];     // 0x5E7
        uint8_t reserved_5EB[15];    // 0x5EB
        uint8_t unknown_5FA;         // 0x5FA
        uint8_t factory_id[16];      // 0x5FB
        uint8_t sha1_b[20];          // 0x60B
        uint8_t signature_b[64];     // 0x61F
        uint8_t version;             // 0x65F
        uint8_t range_count;         // 0x660
        Range ranges[23];            // 0x661
        Range ranges_copy[23];       // 0x730
        uint8_t reserved_7FF;        // 0x7FF
    };
};
#pragma pack(pop)


const std::map<int32_t, uint32_t> XGD_VERSION_MAP = {
    { 2110383, 1 },
    { 2110367, 2 },
    { 2330127, 3 }
};

constexpr uint32_t XGD_SS_LEADOUT_SECTOR = 4267582;


export uint32_t xgd_version(int32_t layer0_last)
{
    auto it = XGD_VERSION_MAP.find(layer0_last);
    return it == XGD_VERSION_MAP.end() ? 0 : it->second;
}


uint32_t PSN_to_LBA(int32_t psn, int32_t layer0_last)
{
    psn -= 0x30000;
    if(psn < 0)
        psn += 2 * (layer0_last + 1);

    return psn;
}


bool read_security_layer_descriptor(SPTD &sptd, std::vector<uint8_t> &response_data, bool kreon_partial_ss)
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


void merge_xgd3_security_layer_descriptor(SecurityLayerDescriptor &sld, const SecurityLayerDescriptor &sld_kreon)
{
    memcpy(sld.ranges, sld_kreon.ranges, sizeof(sld.ranges));
    memcpy(sld.ranges_copy, sld_kreon.ranges_copy, sizeof(sld.ranges_copy));
    sld.xgd23.xgd3.cpr_mai = sld_kreon.xgd23.xgd2.cpr_mai;

    for(uint32_t i = 0; i < 4; ++i)
    {
        memcpy(sld.xgd23.xgd3.crd[i].unknown, sld_kreon.xgd23.xgd2.crd[i].unknown, 8);
        memcpy(sld.xgd23.xgd3.crd[4 + i].unknown, sld_kreon.xgd23.xgd2.crd[4 + i].unknown, 6);
    }
}


export void get_security_layer_descriptor_ranges(std::vector<Range<uint32_t>> &protection, const std::vector<uint8_t> &security_sector)
{
    auto const &sld = (SecurityLayerDescriptor &)security_sector[0];

    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    for(uint32_t i = 0; i < (uint32_t)sld.range_count; ++i)
    {
        if(xgd_version(layer0_last) == 1 && i >= 16 || xgd_version(layer0_last) != 1 && i != 0 && i != 3)
            continue;

        auto psn_start = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_start));
        auto psn_end = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_end));

        insert_range(protection, { PSN_to_LBA(psn_start, layer0_last), PSN_to_LBA(psn_end, layer0_last) + 1 });
    }
}


export void clean_security_sector(std::vector<uint8_t> &security_sector)
{
    auto &sld = (xbox::SecurityLayerDescriptor &)security_sector[0];

    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    constexpr uint16_t DEFAULT_VALUES[] = { 0x01, 0x5B, 0xB5, 0x10F };

    if(xgd_version(layer0_last) == 2)
    {
        for(std::size_t i = 0; i < std::size(DEFAULT_VALUES); ++i)
        {
            memcpy(&sld.xgd23.xgd2.crd[4 + i].unknown[4], &DEFAULT_VALUES[i], sizeof(uint16_t));
            uint16_t zero = 0;
            memcpy(&sld.xgd23.xgd2.crd[4 + i].unknown[7], &zero, sizeof(uint16_t));
        }
    }
    else if(xgd_version(layer0_last) == 3)
    {
        for(std::size_t i = 0; i < std::size(DEFAULT_VALUES); ++i)
        {
            memcpy(&sld.xgd23.xgd3.crd[4 + i].unknown[4], &DEFAULT_VALUES[i], sizeof(uint16_t));
            memcpy(&sld.xgd23.xgd3.crd[4 + i].unknown[7], &DEFAULT_VALUES[i], sizeof(uint16_t));
        }
    }
}


export READ_DVD_STRUCTURE_LayerDescriptor get_final_layer_descriptor(const READ_DVD_STRUCTURE_LayerDescriptor &layer0_ld, const std::vector<uint8_t> &security_sector)
{
    auto const &sld = (SecurityLayerDescriptor &)security_sector[0];

    auto layer_descriptor = layer0_ld;
    layer_descriptor.layer0_end_sector = sld.ld.layer0_end_sector;

    return layer_descriptor;
}


export std::shared_ptr<Context> initialize(std::vector<Range<uint32_t>> &protection, SPTD &sptd, const READ_DVD_STRUCTURE_LayerDescriptor &layer0_ld, uint32_t sectors_count_capacity, bool partial_ss,
    bool kreon_custom_firmware)
{
    std::vector<uint8_t> security_sector(FORM1_DATA_SIZE);
    if(bool complete_ss = read_security_layer_descriptor(sptd, security_sector, partial_ss); !complete_ss)
        LOG("kreon: failed to get complete security sector");

    auto &sld = (SecurityLayerDescriptor &)security_sector[0];
    int32_t ss_layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    if(!xgd_version(ss_layer0_last))
        return nullptr;

    std::string ss_message = "valid";
    if(xgd_version(ss_layer0_last) == 3)
    {
        ss_message = "invalid";

        // repair XGD3 security sector on supported drives (read leadout)
        if(kreon_custom_firmware)
        {
            std::vector<uint8_t> ss_leadout(FORM1_DATA_SIZE);
            auto status = cmd_read(sptd, ss_leadout.data(), FORM1_DATA_SIZE, XGD_SS_LEADOUT_SECTOR, 1, false);
            if(status.status_code)
                LOG("kreon: failed to read XGD3 security sector lead-out, SCSI ({})", SPTD::StatusMessage(status));
            else
            {
                merge_xgd3_security_layer_descriptor((SecurityLayerDescriptor &)ss_leadout[0], sld);
                security_sector = ss_leadout;
                ss_message = "repaired";
            }
        }
    }

    LOG("kreon: XGD detected (version: {}, security sector: {})", xgd_version(ss_layer0_last), ss_message);
    LOG("");

    int32_t psn_first = sign_extend<24>(endian_swap(layer0_ld.data_start_sector));
    int32_t layer0_last = sign_extend<24>(endian_swap(layer0_ld.layer0_end_sector));
    int32_t ss_psn_first = sign_extend<24>(endian_swap(sld.ld.data_start_sector));

    uint32_t l1_padding_length = ss_psn_first - layer0_last - 1;
    if(xgd_version(ss_layer0_last) == 3)
        l1_padding_length += 4096;

    // extract security sector ranges from security sector
    get_security_layer_descriptor_ranges(protection, security_sector);

    // append L1 padding to skip ranges
    insert_range(protection, { sectors_count_capacity, sectors_count_capacity + l1_padding_length });

    auto xbox = std::make_shared<Context>();
    xbox->security_sector.swap(security_sector);
    xbox->lock_lba_start = sectors_count_capacity + l1_padding_length;
    xbox->layer1_video_lba_start = layer0_last + 1 - psn_first;
    return xbox;
}

}
