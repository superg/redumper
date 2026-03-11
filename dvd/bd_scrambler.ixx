module;
#include <climits>
#include <cstdint>
#include <span>

export module bd.scrambler;



namespace gpsxre::bd
{

export class Scrambler
{
public:
    static const Scrambler &get()
    {
        static const Scrambler instance;
        return instance;
    }


    void descramble(std::span<uint8_t> data, int32_t lba, bool nintendo) const
    {
        process(data, nintendo ? seedNintendo(lba) : seedBluray(lba));
    }

private:
    Scrambler() = default;


    uint16_t seedBluray(int32_t lba) const
    {
        uint32_t psn = lba + 0x100000;
        return psn >> 5;
    }


    uint32_t seedNintendo(int32_t lba) const
    {
        uint32_t m = lba & 0xFFFE0;
        uint32_t local = m & 0xFFFF;
        uint32_t k = local >> 8;

        uint32_t slope = (local & 0xFF) << 7;
        uint32_t layer = (m >> 16) & 0xF;

        uint32_t jump4k = ((local >> 12) & 0x3) << 9;
        uint32_t jump32k = (local >> 15) << 12;

        uint32_t toggle = (k & 1) << 5;
        uint32_t decay = k << 4;

        return slope + 1248 + layer + toggle + jump4k + jump32k - decay;
    }


    void process(std::span<uint8_t> data, uint16_t seed) const
    {
        // ISO/IEC 30190

        uint16_t shift_register = (1 << 15) | (seed & 0x7FFF);

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
