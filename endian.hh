#pragma once



#include <algorithm>
#include <array>
#include <cstdint>



namespace gpsxre
{

template <typename T>
T endian_swap(const T &v)
{
	union U
	{
		T v;
		std::array<uint8_t, sizeof(T)> raw;
	} src, dst;

	src.v = v;
	std::reverse_copy(src.raw.begin(), src.raw.end(), dst.raw.begin());
	return dst.v;
}


template<> uint16_t endian_swap<uint16_t>(const uint16_t &v);
template<> uint32_t endian_swap<uint32_t>(const uint32_t &v);
template<> uint64_t endian_swap<uint64_t>(const uint64_t &v);

}
