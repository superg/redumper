module;
#include <cstdint>
#include <vector>

export module utils.xbox;



namespace gpsxre
{

export enum class XDG_Type : uint8_t
{
    UNKNOWN,
    XDG1,
    XDG2,
    XDG3
};

export XDG_Type get_xdg_type(std::vector<uint8_t> &ss)
{
    if(ss.size() != 2048)
        return XDG_Type::UNKNOWN;

    // Concatenate the last three values
    uint32_t xdg_type = ((uint32_t)ss[13] << 16) | ((uint32_t)ss[14] << 8) | ss[15];

    // Return XGD type based on value
    switch(xdg_type)
    {
    case 0x2033AF:
        return XDG_Type::XDG1;
    case 0x20339F:
        return XDG_Type::XDG2;
    case 0x238E0F:
        return XDG_Type::XDG3;
    default:
        return XDG_Type::UNKNOWN;
    }
}

}
