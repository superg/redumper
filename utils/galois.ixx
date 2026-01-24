module;
#include <cstdint>

export module utils.galois;



namespace gpsxre
{

export struct GF256
{
    uint8_t exp[512];
    int32_t log[256];

    explicit GF256(uint16_t prim = 0x11D)
    {
        // set exponential LUT
        exp[0] = 1;
        for(uint8_t i = 1; i < 255; ++i)
        {
            uint16_t x = exp[i - 1] << 1;
            if(x & 0x100)
                x ^= prim;
            exp[i] = uint8_t(x);
        }
        // extend exp to prevent mod math in mul
        for(uint16_t i = 255; i < 512; ++i)
            exp[i] = exp[i - 255];

        // set logarithm LUT
        for(uint8_t i = 0; i < 255; ++i)
            log[i] = -1;
        for(uint8_t i = 0; i < 255; ++i)
            log[exp[i]] = i;
        log[0] = -1;
    }

    // add a + b in GF(2^8)
    uint8_t add(uint8_t a, uint8_t b) const
    {
        return a ^ b;
    }

    // multiply a * b in GF(2^8)
    uint8_t mul(uint8_t a, uint8_t b) const
    {
        if(a == 0 || b == 0)
            return 0;
        return exp[log[a] + log[b]];
    }
};

}
