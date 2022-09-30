#include <cstdlib>
#include "endian.hh"



namespace gpsxre
{

template<>
uint16_t endian_swap<uint16_t>(const uint16_t &v)
{
#ifdef _MSC_VER
	return _byteswap_ushort(v);
#else
	return __builtin_bswap16(v);
#endif
}


template<>
uint32_t endian_swap<uint32_t>(const uint32_t &v)
{
#ifdef _MSC_VER
	return _byteswap_ulong(v);
#else
	return __builtin_bswap32(v);
#endif
}


template<>
uint64_t endian_swap<uint64_t>(const uint64_t &v)
{
#ifdef _MSC_VER
	return _byteswap_uint64(v);
#else
	return __builtin_bswap64(v);
#endif
}

}
