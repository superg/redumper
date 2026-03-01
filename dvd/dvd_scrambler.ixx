module;
#include <climits>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module dvd.scrambler;

import cd.cdrom;
import utils.endian;
import utils.misc;



namespace gpsxre::dvd
{

export constexpr uint32_t ECC_FRAMES = 0x10;


export class Scrambler
{
public:
    static const Scrambler &get()
    {
        static const Scrambler instance;
        return instance;
    }


    void descramble(std::span<uint8_t> data, uint32_t psn, std::optional<uint8_t> nintendo_key) const
    {
        // determine XOR table offset
        uint32_t offset = (psn >> 4 & 0xF) * FORM1_DATA_SIZE;

        // custom XOR table offset for nintendo
        if(nintendo_key)
            offset = (*nintendo_key ^ (psn >> 4 & 0xF)) * FORM1_DATA_SIZE + 7 * FORM1_DATA_SIZE + FORM1_DATA_SIZE / 2;

        process(data, offset);
    }

private:
    std::vector<uint8_t> _table;


    Scrambler()
        : _table(FORM1_DATA_SIZE * ECC_FRAMES)
    {
        // ECMA-267

        uint16_t shift_register = 0x0001;

        for(auto &t : _table)
        {
            t = (uint8_t)shift_register;

            for(uint8_t b = 0; b < CHAR_BIT; ++b)
            {
                // new LSB = b14 XOR b10
                auto lsb = (shift_register >> 14 & 1) ^ (shift_register >> 10 & 1);
                // 15-bit register requires masking MSB
                shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
            }
        }
    }


    void process(std::span<uint8_t> data, uint32_t table_offset) const
    {
        for(uint32_t i = 0; i < data.size(); ++i)
        {
            // wrap table (restart at index 1, not 0)
            uint32_t index = table_offset + i;
            if(index >= _table.size())
                index -= _table.size() - 1;

            data[i] ^= _table[index];
        }
    }
};

}
