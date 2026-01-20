module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

export module dvd.scrambler;

import cd.cdrom;
import dvd;
import dvd.edc;
import utils.endian;
import utils.hex_bin;
import utils.misc;



namespace gpsxre
{

export class DVD_Scrambler
{
public:
    bool descramble(uint8_t *sector, uint32_t psn, std::optional<uint8_t> key, uint32_t size = DATA_FRAME_SIZE) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < sizeof(DataFrame::id) + sizeof(DataFrame::ied))
            return unscrambled;

        auto frame = (DataFrame *)sector;

        // validate sector header
        if(id_to_psn(frame->id) != psn || !validate_id(sector))
            return unscrambled;

        // determine XOR table offset
        uint32_t offset = (psn >> 4 & 0xF) * FORM1_DATA_SIZE;

        // custom XOR table offset for nintendo
        if(key)
            offset = ((*key ^ (psn >> 4 & 0xF)) + 7.5) * FORM1_DATA_SIZE;

        // unscramble sector
        process(sector, sector, offset, size);

        if(endian_swap(frame->edc) == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, offset, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size)
    {
        uint32_t main_data_offset = offsetof(DataFrame, main_data);
        uint32_t end_byte = size < offsetof(DataFrame, edc) ? size : offsetof(DataFrame, edc);
        for(uint32_t i = main_data_offset; i < end_byte; ++i)
        {
            uint32_t index = offset + i - main_data_offset;
            // wrap table
            if(index >= _TABLE_LENGTH)
                index -= (_TABLE_LENGTH - 1);
            output[i] = data[i] ^ _TABLE[index];
        }
    }

private:
    static constexpr uint32_t _TABLE_LENGTH = FORM1_DATA_SIZE * ECC_FRAMES;
    static constexpr auto _TABLE = []()
    {
        std::array<uint8_t, _TABLE_LENGTH> table{};

        // ECMA-267

        uint16_t shift_register = 0x0001;

        for(uint16_t i = 0; i < _TABLE_LENGTH; ++i)
        {
            table[i] = (uint8_t)shift_register;

            for(uint8_t b = 0; b < CHAR_BIT; ++b)
            {
                // new LSB = b14 XOR b10
                auto lsb = (shift_register >> 14 & 1) ^ (shift_register >> 10 & 1);
                // 15-bit register requires masking MSB
                shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
            }
        }

        return table;
    }();
};

}
