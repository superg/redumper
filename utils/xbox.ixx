module;
#include <algorithm>
#include <cstdint>
#include <vector>

export module utils.xbox;



namespace gpsxre
{

export enum class XGD_Type : uint8_t
{
    UNKNOWN,
    XGD1,
    XGD2,
    XGD3
};

export XGD_Type get_xgd_type(const std::vector<uint8_t> &ss)
{
    if(ss.size() != 2048)
        return XGD_Type::UNKNOWN;

    // Concatenate the last three values
    const uint32_t xgd_type = ((uint32_t)ss[13] << 16) | ((uint32_t)ss[14] << 8) | ss[15];

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

export void clean_xbox_security_sector(std::vector<uint8_t> &ss)
{
    XGD_Type xgd_type = get_xgd_type(ss);

    bool ssv2 = false;
    switch(xgd_type)
    {
    case XGD_Type::XGD1:
        // no fix needed
        break;

    case XGD_Type::XGD2:
        ss[552] = 0x01;
        ss[553] = 0x00;
        ss[555] = 0x00;
        ss[556] = 0x00;

        ss[561] = 0x5B;
        ss[562] = 0x00;
        ss[564] = 0x00;
        ss[565] = 0x00;

        ss[570] = 0xB5;
        ss[571] = 0x00;
        ss[573] = 0x00;
        ss[574] = 0x00;

        ss[579] = 0x0F;
        ss[580] = 0x01;
        ss[582] = 0x00;
        ss[583] = 0x00;
        break;

    case XGD_Type::XGD3:
        // determine if ssv1 (Kreon) or ssv2 (0800)
        ssv2 = std::any_of(ss.begin() + 32, ss.begin() + 32 + 72, [](uint8_t x) { return x != 0; });

        if(ssv2)
        {
            ss[72] = 0x01;
            ss[73] = 0x00;
            ss[75] = 0x01;
            ss[76] = 0x00;

            ss[81] = 0x5B;
            ss[82] = 0x00;
            ss[84] = 0x5B;
            ss[85] = 0x00;

            ss[90] = 0xB5;
            ss[91] = 0x00;
            ss[93] = 0xB5;
            ss[94] = 0x00;

            ss[99] = 0x0F;
            ss[100] = 0x01;
            ss[102] = 0x0F;
            ss[103] = 0x01;
        }
        else
        {
            ss[552] = 0x01;
            ss[553] = 0x00;

            ss[561] = 0x5B;
            ss[562] = 0x00;

            ss[570] = 0xB5;
            ss[571] = 0x00;

            ss[579] = 0x0F;
            ss[580] = 0x01;
        }
        break;

    case XGD_Type::UNKNOWN:
    default:
        // cannot clean
        break;
    }
}

}
