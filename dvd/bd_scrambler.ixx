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


    void descramble(std::span<uint8_t> data, uint32_t psn) const
    {
        process(data, psn);
    }

private:
    Scrambler() = default;


    void process(std::span<uint8_t> data, uint32_t psn) const
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
