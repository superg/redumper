#pragma once



#include <climits>
#include <cstdint>
#include <string>
#include <vector>



namespace gpsxre
{

template<typename T, typename U>
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


template<typename T>
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


std::string hexdump(const uint8_t *data, uint32_t offset, uint32_t size);

}
