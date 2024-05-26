module;
#include <algorithm>
#include <cstdint>

export module cd.ecc;

import cd.cdrom;



namespace gpsxre
{

export class ECC
{
public:
    ECC()
    {
        if(!_initialized)
        {
            InitLUTs();
            _initialized = true;
        }
    }


    Sector::ECC Generate(const uint8_t *data)
    {
        Sector::ECC ecc;

        // Compute ECC P code
        ComputeBlock(ecc.p_parity, data, 86, 24, 2, 86);

        // Compute ECC Q code
        ComputeBlock(ecc.q_parity, data, 52, 43, 86, 88);

        return ecc;
    }


    Sector::ECC Generate(Sector &sector, bool zero_address)
    {
        Sector::ECC ecc;

        // Save the address and zero it out
        Sector::Header header;
        if(zero_address)
        {
            header = sector.header;
            std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0);
        }

        // Compute ECC P code
        ComputeBlock(ecc.p_parity, (uint8_t *)&sector.header, 86, 24, 2, 86);

        // Compute ECC Q code
        ComputeBlock(ecc.q_parity, (uint8_t *)&sector.header, 52, 43, 86, 88);

        // Restore the address
        if(zero_address)
            sector.header = header;

        return ecc;
    }

private:
    static constexpr uint32_t _LUT_SIZE = 0x100;
    static uint8_t _F_LUT[_LUT_SIZE];
    static uint8_t _B_LUT[_LUT_SIZE];
    static bool _initialized;


    void InitLUTs()
    {
        for(uint32_t i = 0; i < _LUT_SIZE; ++i)
        {
            _F_LUT[i] = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
            _B_LUT[i ^ _F_LUT[i]] = i;
        }
    }


    // compute ECC for a block (can do either P or Q)
    void ComputeBlock(uint8_t *parity, const uint8_t *data, uint32_t major_count, uint32_t minor_count, uint32_t major_mult, uint32_t minor_inc)
    {
        uint32_t size = major_count * minor_count;
        for(uint32_t major = 0; major < major_count; ++major)
        {
            uint32_t index = (major >> 1) * major_mult + (major & 1);

            uint8_t ecc_a = 0;
            uint8_t ecc_b = 0;
            for(uint32_t minor = 0; minor < minor_count; ++minor)
            {
                uint8_t temp = data[index];
                index += minor_inc;
                if(index >= size)
                    index -= size;
                ecc_a ^= temp;
                ecc_b ^= temp;
                ecc_a = _F_LUT[ecc_a];
            }

            parity[major] = _B_LUT[_F_LUT[ecc_a] ^ ecc_b];
            parity[major + major_count] = parity[major] ^ ecc_b;
        }
    }
};


bool ECC::_initialized(false);
uint8_t ECC::_F_LUT[_LUT_SIZE];
uint8_t ECC::_B_LUT[_LUT_SIZE];

}
