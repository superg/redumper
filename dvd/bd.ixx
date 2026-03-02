module;
#include <cstddef>
#include <cstdint>
#include <span>

export module bd;

import cd.cdrom;
import bd.scrambler;
import common;
import dvd.edc;
import utils.endian;



namespace gpsxre::bd
{

export constexpr int32_t LBA_START = -0x100000;
export constexpr int32_t LBA_IZ = -0x2000;
export constexpr uint32_t ECC_FRAMES = 0x20;


export struct DataFrame
{
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;


    bool valid(int32_t lba) const
    {
        bool valid = false;

        auto df = *this;
        df.descramble(lba);

        if(endian_swap(df.edc) == DVD_EDC().update((uint8_t *)&df, offsetof(DataFrame, edc)).final())
            valid = true;

        return valid;
    }


    void descramble(int32_t lba)
    {
        Scrambler::get().descramble(std::span((uint8_t *)this, sizeof(DataFrame)), lba - LBA_START);
    }
};


#pragma pack(push, 1)
export struct OmniDriveDataFrame
{
    DataFrame data_frame;
    uint8_t unknown[18];
};
#pragma pack(pop)

}
