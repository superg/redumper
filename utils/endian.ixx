module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>

export module utils.endian;



namespace gpsxre
{

export template<typename T>
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


export template<>
uint16_t endian_swap<uint16_t>(const uint16_t &v)
{
#ifdef _MSC_VER
    return _byteswap_ushort(v);
#else
    return __builtin_bswap16(v);
#endif
}


export template<>
uint32_t endian_swap<uint32_t>(const uint32_t &v)
{
#ifdef _MSC_VER
    return _byteswap_ulong(v);
#else
    return __builtin_bswap32(v);
#endif
}


export template<>
uint64_t endian_swap<uint64_t>(const uint64_t &v)
{
#ifdef _MSC_VER
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
}


export template<typename T, typename U, std::size_t N>
T endian_swap_from_array(const U (&a)[N])
{
    T v = 0;

    for(size_t i = 0; i < N; ++i)
        v |= (T)a[i] << CHAR_BIT * sizeof(U) * (N - 1 - i);

    return v;
}


export template<typename T, typename U, std::size_t N>
void endian_swap_to_array(U (&a)[N], T v)
{
    for(size_t i = 0; i < N; ++i)
        a[i] = (U)(v >> CHAR_BIT * sizeof(U) * (N - 1 - i));
}

}
