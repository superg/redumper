module;
#include <cstdint>
#include <numeric>
#include <span>

export module dvd.nintendo;



namespace gpsxre::nintendo
{

export uint8_t derive_key(std::span<const uint8_t> cpr_mai)
{
    auto sum = std::accumulate(cpr_mai.begin(), cpr_mai.end(), 0);
    return ((sum >> 4) + sum) & 0xF;
}

}
