module;
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module bd.scrambler;

import bd;
import cd.cdrom;
import dvd;
import dvd.edc;
import utils.endian;
import utils.misc;



namespace gpsxre::bd
{

void process(std::span<uint8_t> data, uint32_t psn)
{
    // ISO/IEC 30190

    uint16_t shift_register = (1 << 15) | ((psn >> 5) & 0x7FFF);

    for(uint16_t i = 0; i < data.size(); ++i)
    {
        data[i] ^= (uint8_t)shift_register;
        for(uint8_t b = 0; b < CHAR_BIT; ++b)
            shift_register = (shift_register << 1) | (shift_register >> 15 & 1) ^ (shift_register >> 14 & 1) ^ (shift_register >> 12 & 1) ^ (shift_register >> 3 & 1);
    }
}


export bool descramble(BlurayDataFrame &bdf, uint32_t psn)
{
    bool descrambled = false;

    std::span data(bdf.main_data, FORM1_DATA_SIZE);

    // unscramble sector
    process(data, psn);

    if(endian_swap(bdf.edc) == DVD_EDC().update((uint8_t *)&bdf, offsetof(BlurayDataFrame, edc)).final())
        descrambled = true;

    // if EDC does not match, scramble sector back
    if(!descrambled)
        process(data, psn);

    return descrambled;
}

}
