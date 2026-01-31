module;
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module dvd.scrambler;

import cd.cdrom;
import dvd;
import dvd.edc;
import utils.endian;
import utils.misc;



namespace gpsxre
{

export class DVD_Scrambler
{
public:
    DVD_Scrambler()
        : _table(FORM1_DATA_SIZE * 0x10)
    {
        // ECMA-267

        uint16_t shift_register = 0x0001;

        for(uint16_t i = 0; i < _table.size(); ++i)
        {
            _table[i] = (uint8_t)shift_register;

            for(uint8_t b = 0; b < CHAR_BIT; ++b)
            {
                // new LSB = b14 XOR b10
                auto lsb = (shift_register >> 14 & 1) ^ (shift_register >> 10 & 1);
                // 15-bit register requires masking MSB
                shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
            }
        }
    }


    bool descramble(uint8_t *sector, uint32_t psn, std::optional<uint8_t> key, uint32_t size = sizeof(DataFrame)) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < sizeof(DataFrame::id) + sizeof(DataFrame::ied))
            return unscrambled;

        auto frame = (DataFrame *)sector;

        // validate sector header
        if(endian_swap_from_array<int32_t>(frame->id.sector_number) != psn || !validate_id(sector))
            return unscrambled;

        // determine XOR table offset
        uint32_t offset = (psn >> 4 & 0xF) * FORM1_DATA_SIZE;

        // custom XOR table offset for nintendo
        if(key)
            offset = (*key ^ (psn >> 4 & 0xF)) * FORM1_DATA_SIZE + 7 * FORM1_DATA_SIZE + FORM1_DATA_SIZE / 2;

        // unscramble sector
        process(sector, sector, offset, size);

        if(endian_swap(frame->edc) == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, offset, size);

        return unscrambled;
    }


    void process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size) const
    {
        uint32_t main_data_offset = offsetof(DataFrame, main_data);
        for(uint32_t i = main_data_offset; i < std::min(size, (uint32_t)offsetof(DataFrame, edc)); ++i)
        {
            uint32_t index = offset + i - main_data_offset;
            // wrap table (restart at index 1, not 0)
            if(index >= _table.size())
                index -= (_table.size() - 1);
            output[i] = data[i] ^ _table[index];
        }
    }

private:
    std::vector<uint8_t> _table;
};

}
