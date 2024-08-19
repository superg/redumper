module;
#include <algorithm>
#include <cstdint>
#include <vector>

export module utils.xbox;

import scsi.mmc;
import utils.endian;



namespace gpsxre
{

export enum class XGD_Type : uint8_t
{
    UNKNOWN,
    XGD1,
    XGD2,
    XGD3
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

}
