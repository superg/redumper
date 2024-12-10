module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "throw_line.hh"

export module utils.xbox;

import dump;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;



namespace gpsxre
{

export enum class XGD_Type : uint8_t
{
    UNKNOWN,
    XGD1,
    XGD2,
    XGD3
};

export struct XGD1_SecuritySector
{
    uint8_t reserved1[703];
    uint32_t cpr_mai;
    uint8_t reserved2[44];
    uint8_t ccrt_version;
    uint8_t ccrt_length;
    uint8_t ccrt[285];
    uint64_t creation_filetime;
    uint8_t unknown2[16];
    uint32_t reserved3;
    uint8_t unknown3[16];
    uint8_t reserved4[84];
    uint64_t authoring_filetime;
    uint32_t cert_time_t;
    uint8_t reserved5[15];
    uint8_t typeA;
    uint8_t game_id[16];
    uint8_t sha1A[20];
    uint8_t signatureA[256];
    uint64_t mastering_filetime;
    uint8_t reserved6[19];
    uint8_t typeB;
    uint8_t factory_id[16];
    uint8_t sha1B[20];
    uint8_t signatureB[64];
    uint8_t ss_version;
    uint8_t ranges_length;
    uint8_t security_ranges[207];
    uint8_t security_ranges_duplicate[207];
    uint8_t reserved7;
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

export bool xbox_get_security_sector(SPTD &sptd, std::vector<uint8_t> &response_data)
{
    SPTD::Status status;

    const uint8_t ss_vals[4] = { 0x01, 0x03, 0x05, 0x07 };
    for(int i = 0; i < sizeof(ss_vals); ++i)
    {
        status = cmd_kreon_get_security_sector(sptd, response_data, ss_vals[i]);
        if(status.status_code)
        {
            // fail if cannot get initial response, otherwise just note partial response
            if(i == 0)
                throw_line("failed to get security sector, SCSI ({})", SPTD::StatusMessage(status));

            return false;
        }
    }

    return true;
}

export void generate_extra_xbox(std::string &image_prefix)
{
    // Only generate extra xbox files if a .security exists
    std::filesystem::path security_path(image_prefix + ".security");
    std::filesystem::path ss_path(image_prefix + ".security");
    if(std::filesystem::exists(security_path))
    {
        // trim the 4 byte header from .manufacturer and write it to a .dmi (if it doesn't exist)
        std::filesystem::path manufacturer_path(image_prefix + ".manufacturer");
        std::filesystem::path dmi_path(image_prefix + ".dmi");
        if(std::filesystem::exists(manufacturer_path) && !std::filesystem::exists(dmi_path))
        {
            auto manufacturer = read_vector(manufacturer_path);
            if(!manufacturer.empty() && manufacturer.size() == 2052)
            {
                manufacturer.erase(manufacturer.begin(), manufacturer.begin() + 4);
                write_vector(dmi_path, manufacturer);
            }
            else
            {
                LOG("warning: could not generate DMI, unexpected file size ({})", manufacturer_path.filename().string());
            }
        }

        // trim the 4 byte header from .physical and write it to a .pfi (if it doesn't exist)
        std::filesystem::path physical_path(image_prefix + ".physical");
        std::filesystem::path pfi_path(image_prefix + ".pfi");
        if(std::filesystem::exists(physical_path) && !std::filesystem::exists(pfi_path))
        {
            auto physical = read_vector(physical_path);
            if(!physical.empty() && physical.size() == 2052)
            {
                physical.erase(physical.begin(), physical.begin() + 4);
                write_vector(pfi_path, physical);
            }
            else
            {
                LOG("warning: could not generate PFI, unexpected file size ({})", physical_path.filename().string());
            }
        }

        // clean the .security and write it to a .ss (if it doesn't exist)
        std::filesystem::path ss_path(image_prefix + ".ss");
        if(!std::filesystem::exists(ss_path))
        {
            auto security = read_vector(security_path);
            if(!security.empty() && security.size() == 2048)
            {
                clean_xbox_security_sector(security);
                write_vector(ss_path, security);

                LOG("security sector ranges:");
                auto security_ranges = get_security_sector_ranges((READ_DVD_STRUCTURE_LayerDescriptor &)security[0]);
                for(const auto &range : security_ranges)
                {
                    LOG("  {}-{}", range.first, range.second);
                }
            }
            else
            {
                LOG("warning: could not generate SS, unexpected file size ({})", security_path.filename().string());
            }
        }
    }
}

}
