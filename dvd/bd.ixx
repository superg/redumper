module;
#include <cstdint>
#include <cstring>
#include "throw_line.hh"

export module bd;

import cd.cdrom;
import common;



namespace gpsxre
{

export constexpr int32_t BD_LBA_START = -0x100000;
export constexpr int32_t BD_LBA_IZ = -0x2000;
export constexpr uint32_t BD_ECC_FRAMES = 0x20;


export struct BlurayDataFrame
{
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;
};


export struct OmniDriveBlurayDataFrame
{
    BlurayDataFrame data_frame;
    uint8_t unknown[20];
};

}
