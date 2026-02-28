module;
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>

export module dvd.nintendo;

import dvd;
import dvd.scrambler;



namespace gpsxre::nintendo
{

uint8_t derive_key(std::span<const uint8_t> cpr_mai)
{
    auto sum = std::accumulate(cpr_mai.begin(), cpr_mai.end(), 0);
    return ((sum >> 4) + sum) & 0xF;
}


export std::optional<uint8_t> get_key(std::optional<uint8_t> &key_lba0, int32_t lba, const dvd::DataFrame &data_frame)
{
    std::optional<uint8_t> key;

    if(key_lba0 && lba >= 0)
    {
        key = lba < (int32_t)dvd::ECC_FRAMES ? 0 : *key_lba0;

        if(lba == 0)
        {
            auto df = data_frame;
            df.descramble(key);
            *key_lba0 = derive_key(std::span(df.cpr_mai, df.cpr_mai + 8));
        }
    }

    return key;
}

}
