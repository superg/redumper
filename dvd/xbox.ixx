module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "throw_line.hh"

export module dvd.xbox;

import cd.cdrom;
import drive;
import dvd;
import dvd.edc;
import hash.aes128;
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
    struct ChallengeResponse
    {
        uint8_t cdata[4];

        union
        {
            struct
            {
                uint8_t rdata[4];
                uint8_t flag;
            };

            struct
            {
                uint16_t angle_a;
                uint8_t reserved;
                uint16_t angle_b;
            };
        };
    };

    struct ChallengeEntry
    {
        uint8_t type;
        uint8_t cid;
        uint8_t tolerance;
        uint8_t ctype;
        uint8_t cdata[4];
        uint8_t rdata[4];
    };

    struct DriveResponse
    {
        uint8_t type;
        uint8_t cid;
        uint8_t mod;

        union
        {
            uint8_t data[6];

            struct
            {
                uint8_t psn_start[3];
                uint8_t psn_end[3];
            };
        };
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
                ChallengeEntry ccrt[21];  // 0x304
                uint8_t reserved_400[96]; // 0x400
                uint8_t media_id[16];     // 0x460
                uint8_t reserved_470[46]; // 0x470
                uint8_t unknown_49E;      // 0x49E
            } xgd23;
        };

        uint64_t authoring_filetime;        // 0x49F
        uint32_t cert_unixtime;             // 0x4A7
        uint8_t reserved_4AB[15];           // 0x4AB
        uint8_t unknown_4BA;                // 0x4BA
        uint8_t game_id[16];                // 0x4BB
        uint8_t sha1_a[20];                 // 0x4CB
        uint8_t signature_a[256];           // 0x4DF
        uint64_t mastering_filetime;        // 0x5DF
        uint8_t reserved_5E7[4];            // 0x5E7
        uint8_t reserved_5EB[15];           // 0x5EB
        uint8_t unknown_5FA;                // 0x5FA
        uint8_t factory_id[16];             // 0x5FB
        uint8_t sha1_b[20];                 // 0x60B
        uint8_t signature_b[64];            // 0x61F
        uint8_t version;                    // 0x65F
        uint8_t range_count;                // 0x660
        DriveResponse drive_table[23];      // 0x661
        DriveResponse drive_table_copy[23]; // 0x730
        uint8_t reserved_7FF;               // 0x7FF
    };
};
#pragma pack(pop)

export struct DMI
{
    uint8_t version;

    union
    {
        struct
        {
            uint8_t reserved_001[7];
            union
            {
                struct
                {
                    char publisher_id[2];
                    char game_id[3];
                    char sku[2];
                    char region;
                } xmid;
                char xmid_string[8];
            };
            uint8_t timestamp[8];
            uint8_t xor_key_id;
            uint8_t reserved_019[55];
        } xgd1;

        struct
        {
            uint8_t reserved_001[15];
            uint8_t timestamp[8];
            uint8_t xor_key_id;
            uint8_t reserved_019[7];
            uint8_t media_id[16];
            uint8_t reserved_030[16];
            union
            {
                struct
                {
                    char publisher_id[2];
                    char game_id[4];
                    char sku[2];
                    char region;
                    union
                    {
                        struct
                        {
                            char version_id;
                            char media_id;
                            char disc_number;
                            char disc_total;
                            char reserved_04E[3];
                        } short_version;

                        struct
                        {
                            char version_id[2];
                            char media_id;
                            char disc_number;
                            char disc_total;
                            char reserved_04E[2];
                        } long_version;
                    };
                } xemid;
                char xemid_string[16];
            };
        } xgd23;
    };

    uint8_t reserved_050[1508];
    uint8_t dmi_trailer[460];
};


const std::map<int32_t, uint32_t> XGD_VERSION_MAP = {
    { 2110383, 1 },
    { 2110367, 2 },
    { 2330127, 3 }
};

constexpr uint32_t XGD_SS_LEADOUT_SECTOR = 0x00FD021E;


export uint32_t xgd_version(int32_t layer0_last)
{
    auto it = XGD_VERSION_MAP.find(layer0_last);
    return it == XGD_VERSION_MAP.end() ? 0 : it->second;
}


int32_t PSN_to_LBA(int32_t psn, int32_t layer0_last)
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
    memcpy(sld.drive_table, sld_kreon.drive_table, sizeof(sld.drive_table));
    memcpy(sld.drive_table_copy, sld_kreon.drive_table_copy, sizeof(sld.drive_table_copy));
    sld.xgd23.xgd3.cpr_mai = sld_kreon.xgd23.xgd2.cpr_mai;

    for(uint32_t i = 0; i < 4; ++i)
    {
        memcpy(&sld.xgd23.xgd3.crd[i], &sld_kreon.xgd23.xgd2.crd[i], 8);
        memcpy(&sld.xgd23.xgd3.crd[4 + i], &sld_kreon.xgd23.xgd2.crd[4 + i], 6);
    }
}


export void get_security_layer_descriptor_ranges(std::vector<Range<int32_t>> &protection, const std::vector<uint8_t> &security_sector)
{
    auto const &sld = (SecurityLayerDescriptor &)security_sector[0];

    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    for(uint32_t i = 0; i < (uint32_t)sld.range_count; ++i)
    {
        if(xgd_version(layer0_last) == 1 && i >= 16 || xgd_version(layer0_last) != 1 && i != 0 && i != 3)
            continue;

        auto psn_start = sign_extend<24>(endian_swap_from_array<int32_t>(sld.drive_table[i].psn_start));
        auto psn_end = sign_extend<24>(endian_swap_from_array<int32_t>(sld.drive_table[i].psn_end));

        insert_range(protection, { PSN_to_LBA(psn_start, layer0_last), PSN_to_LBA(psn_end, layer0_last) + 1 });
    }
}


export bool clean_security_sector(std::vector<uint8_t> &security_sector)
{
    auto &sld = (SecurityLayerDescriptor &)security_sector[0];
    int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));
    uint32_t xgd = xgd_version(layer0_last);

    constexpr uint32_t challenge_entries = sizeof(sld.xgd23.ccrt) / sizeof(sld.xgd23.ccrt[0]);

    auto &drive_responses = (std::array<SecurityLayerDescriptor::DriveResponse, challenge_entries> &)sld.drive_table_copy;

    // rebuild challenge table
    bool valid = true;
    if(xgd == 2 || xgd == 3)
    {
        constexpr std::array<uint8_t, AES128::BLOCK_SIZE> key{ 0xD1, 0xE3, 0xB3, 0x3A, 0x6C, 0x1E, 0xF7, 0x70, 0x5F, 0x6D, 0xE9, 0x3B, 0xB6, 0xC0, 0xDC, 0x71 };
        constexpr uint32_t ccrt_cbc_size = 15 * AES128::BLOCK_SIZE;

        // decrypt first 15 blocks
        const AES128 aes(key);
        std::array<uint8_t, AES128::BLOCK_SIZE> iv{};
        std::array<uint8_t, sizeof(sld.xgd23.ccrt)> dcrt_bytes{};
        for(uint32_t offset = 0; offset < ccrt_cbc_size; offset += AES128::BLOCK_SIZE)
        {
            std::array<uint8_t, AES128::BLOCK_SIZE> ciphertext;
            std::copy_n((const uint8_t *)sld.xgd23.ccrt + offset, ciphertext.size(), ciphertext.begin());

            const auto decrypted = aes.decryptBlock(ciphertext);
            for(uint32_t i = 0; i < AES128::BLOCK_SIZE; ++i)
                dcrt_bytes[offset + i] = decrypted[i] ^ iv[i];

            iv = ciphertext;
        }

        // copy remainder decrypted
        std::copy_n((const uint8_t *)sld.xgd23.ccrt + ccrt_cbc_size, sizeof(dcrt_bytes) - ccrt_cbc_size, dcrt_bytes.begin() + ccrt_cbc_size);

        auto &dcrt = (std::array<SecurityLayerDescriptor::ChallengeEntry, challenge_entries> &)dcrt_bytes;

        // collate CID map
        std::map<uint8_t, uint32_t> challenge_ids;
        for(uint32_t i = 0; i < challenge_entries; ++i)
        {
            if(!challenge_ids.emplace(dcrt[i].cid, i).second)
            {
                valid = false;
                break;
            }
        }

        // validate challenge response table
        std::array<uint32_t, challenge_entries> candidate_indices{};
        uint32_t challenge_count = 0;
        for(uint32_t i = 0; valid && i < challenge_entries; ++i)
        {
            auto &response = drive_responses[i];
            auto challenge_it = challenge_ids.find(response.cid);
            if(challenge_it == challenge_ids.end())
            {
                valid = false;
                break;
            }

            candidate_indices[i] = challenge_it->second;
            auto &challenge = dcrt[candidate_indices[i]];

            switch(challenge.type)
            {
            case 0x14:
                if(response.type != 0x03 || ++challenge_count >= 5)
                    valid = false;
                break;
            case 0x15:
                if(response.type != 0x01 || ++challenge_count >= 5)
                    valid = false;
                break;
            case 0x24:
                if(response.type != 0x07 || ++challenge_count < 5 || challenge_count > 8)
                    valid = false;
                break;
            case 0x25:
                if(response.type != 0x05 || ++challenge_count < 5 || challenge_count > 8)
                    valid = false;
                break;
            default:
                if(++challenge_count <= 8)
                    valid = false;
                break;
            }
        }

        if(valid)
        {
            // fill in first 8 entries in replay table
            constexpr uint16_t ANGLE_DEFAULT_VALUES[] = { 0x01, 0x5B, 0xB5, 0x10F };
            SecurityLayerDescriptor::ChallengeResponse *crd = xgd == 3 ? sld.xgd23.xgd3.crd : sld.xgd23.xgd2.crd;
            for(uint32_t i = 0; i < 8; ++i)
            {
                auto &candidate = dcrt[candidate_indices[i]];
                auto &entry = crd[i];

                memcpy(entry.cdata, candidate.cdata, sizeof(entry.cdata));

                if(i >= 4)
                {
                    // angle challenge response normalization
                    uint16_t angle_value = ANGLE_DEFAULT_VALUES[i - 4];
                    entry.angle_a = angle_value;
                    entry.angle_b = xgd == 3 ? angle_value : 0;
                }
                else
                {
                    // fixed value normalization
                    memcpy(entry.rdata, candidate.rdata, sizeof(entry.rdata));
                    entry.flag = 0;
                }
            }
        }
    }

    return valid;
}


export READ_DVD_STRUCTURE_LayerDescriptor get_final_layer_descriptor(const READ_DVD_STRUCTURE_LayerDescriptor &layer0_ld, const std::vector<uint8_t> &security_sector)
{
    auto const &sld = (SecurityLayerDescriptor &)security_sector[0];

    auto layer_descriptor = layer0_ld;
    layer_descriptor.layer0_end_sector = sld.ld.layer0_end_sector;

    return layer_descriptor;
}


export std::shared_ptr<Context> initialize(std::vector<Range<int32_t>> &protection, SPTD &sptd, const READ_DVD_STRUCTURE_LayerDescriptor &layer0_ld, uint32_t sectors_count_capacity, bool partial_ss,
    const DriveConfig &drive_config)
{
    bool kreon = is_kreon_firmware(drive_config);
    bool custom_kreon = kreon && is_custom_kreon_firmware(drive_config);
    bool omnidrive = is_omnidrive_firmware(drive_config) != std::nullopt;
    std::vector<uint8_t> security_sector(FORM1_DATA_SIZE);
    uint32_t cpr_mai_key = 0;
    std::string ss_message = "valid";

    if(kreon)
    {
        if(!read_security_layer_descriptor(sptd, security_sector, partial_ss))
            ss_message = "incomplete";
    }
    else if(omnidrive)
    {
        dvd::DataFrame df;
        bool ss_found = false;
        for(uint8_t ss_retries = 0; ss_retries < 4; ++ss_retries)
        {
            uint32_t ss_address = XGD_SS_LEADOUT_SECTOR + ss_retries * 0x40;
            auto status = cmd_read_omnidrive(sptd, (uint8_t *)&df, sizeof(dvd::DataFrame), ss_address, 1, OmniDrive_DiscType::DVD, true, false, false, OmniDrive_Subchannels::NONE, false);
            if(status.status_code)
                LOG("[PSN: {:X}] omnidrive: SCSI error ({})", ss_address, SPTD::StatusMessage(status));
            else
            {
                if(df.valid())
                {
                    df.descramble();
                    ss_found = true;
                    break;
                }
                else
                    LOG("[PSN: {:X}] omnidrive: invalid security sector, discarded", ss_address);
            }
        }

        if(!ss_found)
        {
            LOG("omnidrive: failed to get valid security sector");
            return nullptr;
        }
        std::copy(df.main_data, df.main_data + FORM1_DATA_SIZE, security_sector.begin());
        cpr_mai_key = (uint32_t &)df.cpr_mai[1];
    }

    auto &sld = (SecurityLayerDescriptor &)security_sector[0];
    int32_t psn_first = sign_extend<24>(endian_swap(layer0_ld.data_start_sector));
    int32_t ss_layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));

    if(!xgd_version(ss_layer0_last))
        return nullptr;

    if(kreon)
    {
        if(xgd_version(ss_layer0_last) == 3)
        {
            ss_message = "invalid";

            // repair XGD3 security sector on supported drives (read leadout)
            if(custom_kreon)
            {
                std::vector<uint8_t> ss_leadout(FORM1_DATA_SIZE);
                uint32_t ss_address = sign_extend<24>(XGD_SS_LEADOUT_SECTOR) + 2 * (ss_layer0_last + 1) - psn_first;
                auto status = cmd_read(sptd, ss_leadout.data(), FORM1_DATA_SIZE, ss_address, 1, false);
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
    }
    else if(omnidrive)
    {
        // copy cpr_mai key into ss
        if(xgd_version(ss_layer0_last) == 1)
            sld.xgd1.cpr_mai = cpr_mai_key;
        else if(xgd_version(ss_layer0_last) == 2)
            sld.xgd23.xgd2.cpr_mai = cpr_mai_key;
        else
            sld.xgd23.xgd3.cpr_mai = cpr_mai_key;

        // descramble drive entries
        std::vector<uint8_t> indices((uint8_t *)&sld.drive_table_copy, (uint8_t *)&sld.drive_table_copy + sizeof(sld.drive_table));
        for(uint8_t i = 0; i < indices.size(); ++i)
            indices[i] ^= ((uint8_t *)&cpr_mai_key)[i % 4];

        std::vector<uint8_t> drive_table(sizeof(sld.drive_table), 0);
        std::vector<uint8_t> drive_table_scrambled((uint8_t *)&sld.drive_table, (uint8_t *)&sld.drive_table + sizeof(sld.drive_table));
        for(uint8_t i = 0; i < indices.size(); ++i)
            drive_table[i] = drive_table_scrambled[indices[i]];

        // copy drive entries into ss
        std::copy(drive_table.begin(), drive_table.end(), (uint8_t *)&sld.drive_table);
        std::copy(drive_table.begin(), drive_table.end(), (uint8_t *)&sld.drive_table_copy);

        if(xgd_version(ss_layer0_last) != 1)
            ss_message = "zeroed";
    }

    LOG("{}: XGD detected (version: {}, security sector: {})", kreon ? "kreon" : "omnidrive", xgd_version(ss_layer0_last), ss_message);
    LOG("");

    int32_t layer0_last = sign_extend<24>(endian_swap(layer0_ld.layer0_end_sector));
    int32_t ss_psn_first = sign_extend<24>(endian_swap(sld.ld.data_start_sector));

    uint32_t l1_padding_length = ss_psn_first - layer0_last - 1;
    if(xgd_version(ss_layer0_last) == 3)
        l1_padding_length += 4096;

    // extract security sector ranges from security sector
    get_security_layer_descriptor_ranges(protection, security_sector);

    // append L1 padding to skip ranges to account for kreon firmware limitations
    if(kreon)
        insert_range(protection, { (int32_t)sectors_count_capacity, (int32_t)sectors_count_capacity + (int32_t)l1_padding_length });

    auto xbox = std::make_shared<Context>();
    xbox->security_sector.swap(security_sector);
    xbox->lock_lba_start = sectors_count_capacity + l1_padding_length;
    xbox->layer1_video_lba_start = layer0_last + 1 - psn_first;
    return xbox;
}

}
