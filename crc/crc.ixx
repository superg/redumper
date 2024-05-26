module;
#include <array>
#include <climits>
#include <cstdint>

export module crc.crc;

import utils.misc;



// generic CRC implementation with compile time table optimization
// invaluable and thoroughly explained resource:
// http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html
//
// catalog of CRC algorithms:
// https://reveng.sourceforge.io/crc-catalogue/all.htm



namespace gpsxre
{

export template<typename T, T polynomial, T seed, T final_xor, bool reflect_input, bool reflect_output, bool reciprocal>
class CRC
{
public:
    CRC()
    {
        reset();
    }


    CRC &reset()
    {
        _crc = reciprocal_reflect(seed);

        return *this;
    }


    CRC &update(const uint8_t *data, uint64_t size)
    {
        for(uint64_t i = 0; i < size; ++i)
            _crc = shift_byte(_crc) ^ _TABLE[(shift_right(_crc) ^ reflect_data(data[i])) & 0xFF];

        return *this;
    }


    T final() const
    {
        return reflect_final(_crc) ^ final_xor;
    }

private:
    template<typename U>
    static constexpr U reflect(U value)
    {
        return bits_reflect(value);
    }


    template<bool rec = reciprocal>
    static constexpr T reciprocal_reflect(typename std::enable_if<!rec, T>::type value)
    {
        return value;
    }
    template<bool rec = reciprocal>
    static constexpr T reciprocal_reflect(typename std::enable_if<rec, T>::type value)
    {
        return reflect(value);
    }


    template<bool rec = reciprocal>
    static constexpr T mask(typename std::enable_if<!rec, void>::type * = nullptr)
    {
        return 1 << (sizeof(T) * CHAR_BIT - 1);
    }
    template<bool rec = reciprocal>
    static constexpr T mask(typename std::enable_if<rec, void>::type * = nullptr)
    {
        return 1;
    }


    template<bool rec = reciprocal>
    static constexpr T shift_bit(typename std::enable_if<!rec, T>::type value)
    {
        return value << 1;
    }
    template<bool rec = reciprocal>
    static constexpr T shift_bit(typename std::enable_if<rec, T>::type value)
    {
        return value >> 1;
    }


    template<bool rec = reciprocal>
    static constexpr T shift_byte(typename std::enable_if<!rec, T>::type value)
    {
        return value << CHAR_BIT;
    }
    template<bool rec = reciprocal>
    static constexpr T shift_byte(typename std::enable_if<rec, T>::type value)
    {
        return value >> CHAR_BIT;
    }


    template<bool rec = reciprocal>
    static constexpr T shift_right(typename std::enable_if<!rec, T>::type value)
    {
        return value >> (sizeof(T) - 1) * CHAR_BIT;
    }
    template<bool rec = reciprocal>
    static constexpr T shift_right(typename std::enable_if<rec, T>::type value)
    {
        return value;
    }


    template<bool rec = reciprocal>
    static constexpr T shift_left(typename std::enable_if<!rec, T>::type value)
    {
        return value << (sizeof(T) - 1) * CHAR_BIT;
    }
    template<bool rec = reciprocal>
    static constexpr T shift_left(typename std::enable_if<rec, T>::type value)
    {
        return value;
    }


    template<bool rec = reciprocal, bool ref = reflect_input>
    static constexpr T reflect_data(typename std::enable_if<!(!rec && ref || rec && !ref), uint8_t>::type value)
    {
        return value;
    }
    template<bool rec = reciprocal, bool ref = reflect_input>
    static constexpr T reflect_data(typename std::enable_if<!rec && ref || rec && !ref, uint8_t>::type value)
    {
        return reflect(value);
    }


    template<bool rec = reciprocal, bool ref = reflect_output>
    static constexpr T reflect_final(typename std::enable_if<!(!rec && ref || rec && !ref), T>::type value)
    {
        return value;
    }
    template<bool rec = reciprocal, bool ref = reflect_output>
    static constexpr T reflect_final(typename std::enable_if<!rec && ref || rec && !ref, T>::type value)
    {
        return reflect(value);
    }


    static constexpr auto _TABLE = []()
    {
        std::array<T, 0x100> table;

        for(T i = 0; i < 0x100; ++i)
        {
            T crc = shift_left(i);

            for(T b = 0; b < CHAR_BIT; ++b)
                crc = shift_bit(crc) ^ (crc & mask() ? reciprocal_reflect(polynomial) : 0);

            table[i] = crc;
        }

        return table;
    }();

    T _crc;
};

}
