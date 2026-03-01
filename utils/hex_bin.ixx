module;
#include <climits>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

export module utils.hex_bin;



namespace gpsxre
{

export template<typename T, typename U>
U hex2bin(T *data, U size, const std::string &hex_string)
{
    U i = 0;
    for(; i < size; ++i)
    {
        data[i] = 0;

        for(U j = 0; j < sizeof(T) * 2; ++j)
        {
            char c = hex_string[i * sizeof(T) * 2 + j];
            if(c == '\0')
                return i;

            uint8_t base;
            if(c >= '0' && c <= '9')
                base = '0';
            else if(c >= 'a' && c <= 'f')
                base = 'a' - 0x0A;
            else if(c >= 'A' && c <= 'F')
                base = 'A' - 0x0A;
            else
                base = c;

            data[i] |= (T)(c - base) << 4 * (sizeof(T) * 2 - 1 - j);
        }
    }

    return i;
}


export template<typename T>
std::string bin2hex(const std::vector<T> &data)
{
    std::string hex_string;

    for(auto &e : data)
    {
        for(uint32_t j = 0; j < sizeof(T); ++j)
        {
            for(uint32_t k = 0; k < 2; ++k)
            {
                uint8_t c = e >> (j * CHAR_BIT + (k ? 0 : CHAR_BIT / 2)) & 0x0F;
                hex_string += c >= 0xA ? 'a' + c - 0xA : '0' + c;
            }
        }
    }

    return hex_string;
}


export std::string hexdump(const uint8_t *data, uint32_t offset, uint32_t size)
{
    std::string dump;

    auto data_offset = data + offset;

    // FIXME: tail < 16 is not included, not needed right now
    uint32_t rows = size / 16;

    std::stringstream ss;
    ss << std::setfill('0');
    for(uint32_t r = 0; r < rows; ++r)
    {
        uint32_t row_offset = r * 16;

        ss << std::hex << std::uppercase;
        ss << std::setw(4) << offset + row_offset << " : ";

        for(uint32_t i = 0; i < 16; ++i)
        {
            if(i == 8)
                ss << ' ';
            ss << std::setw(2) << (uint32_t)data_offset[row_offset + i] << ' ';
        }

        ss << std::dec << "  ";

        for(uint32_t i = 0; i < 16; ++i)
        {
            auto c = data_offset[row_offset + i];
            ss << (c >= 0x20 && c < 0x80 ? (char)c : '.');
        }
        ss << std::endl;
    }

    dump = ss.str();

    return dump;
}

export std::string rawhexdump(const uint8_t *data, uint32_t size, uint32_t group_size = 2, uint32_t groups_per_line = 8)
{
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    for(uint32_t i = 0; i < size; i++)
    {
        if(i > 0 && group_size > 0 && i % group_size == 0)
        {
            if(groups_per_line > 0 && i / group_size % groups_per_line == 0)
            {
                ss << std::endl;
            }
            else
            {
                ss << ' ';
            }
        }
        ss << std::setw(2) << (uint32_t)data[i];
    }

    if(size > 0)
        ss << std::endl;

    return ss.str();
}

}
