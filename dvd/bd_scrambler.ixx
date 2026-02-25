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

export class Scrambler
{
public:
    bool descramble(bd::DataFrame &df, uint32_t psn)
    {
        bool descrambled = false;

        std::span data((uint8_t *)&df, sizeof(bd::DataFrame));

        // unscramble sector
        process(data, psn);

        if(endian_swap(df.edc) == DVD_EDC().update((uint8_t *)&df, offsetof(DataFrame, edc)).final())
            descrambled = true;

        // if EDC does not match, scramble sector back
        if(!descrambled)
            process(data, psn);

        return descrambled;
    }

private:
    void process(std::span<uint8_t> data, uint32_t psn)
    {
        // ISO/IEC 30190

        uint16_t shift_register = (1 << 15) | ((psn >> 5) & 0x7FFF);

        for(auto &byte : data)
        {
            byte ^= (uint8_t)shift_register;

            for(uint8_t b = 0; b < CHAR_BIT; ++b)
            {
                auto lsb = ((shift_register >> 15) ^ (shift_register >> 14) ^ (shift_register >> 12) ^ (shift_register >> 3)) & 1;
                shift_register = (shift_register << 1) | lsb;
            }
        }
    }
};

}
