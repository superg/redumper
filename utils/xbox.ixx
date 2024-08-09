module;
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

}
