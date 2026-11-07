module;
#include <array>
#include <cstdint>
#include <random>
#include "throw_line.hh"

export module hash.edcsa;



namespace gpsxre
{

namespace
{

// 160-bit AACS curve, a = -3 and cofactor = 1.
constexpr std::array<uint32_t, 6> P{ 0x79A7D7DF, 0xF9EAE7C4, 0x60BDB09E, 0x55ECCEB5, 0x9DC9D813 };
constexpr std::array<uint32_t, 6> P_MINUS_2{ 0x79A7D7DD, 0xF9EAE7C4, 0x60BDB09E, 0x55ECCEB5, 0x9DC9D813 };
constexpr std::array<uint32_t, 6> B{ 0xDAACB1D8, 0x1245E0C4, 0x5248D68E, 0xC1CBCD16, 0x402DAD3E };
constexpr std::array<uint32_t, 6> N{ 0x7F5AB017, 0x54817B2C, 0x60BDC44F, 0x55ECCEB5, 0x9DC9D813 };
constexpr std::array<uint32_t, 6> N_MINUS_2{ 0x7F5AB015, 0x54817B2C, 0x60BDC44F, 0x55ECCEB5, 0x9DC9D813 };
constexpr std::array<uint32_t, 6> GX{ 0xC54CCEC6, 0x81D0A4BD, 0xF4CCA7EB, 0x578351E6, 0x2E64FC22 };
constexpr std::array<uint32_t, 6> GY{ 0x07F5CBB9, 0xF23C9A07, 0x9DB455C7, 0xD0544288, 0x0914A25D };


int compare(const std::array<uint32_t, 6> &a, const std::array<uint32_t, 6> &b)
{
    for(uint32_t i = a.size(); i-- > 0;)
    {
        if(a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }

    return 0;
}


bool is_zero(const std::array<uint32_t, 6> &a)
{
    for(auto word : a)
        if(word != 0)
            return false;

    return true;
}


std::array<uint32_t, 6> subtract(std::array<uint32_t, 6> a, const std::array<uint32_t, 6> &b)
{
    uint64_t borrow = 0;
    for(uint32_t i = 0; i < a.size(); ++i)
    {
        uint64_t subtrahend = (uint64_t)b[i] + borrow;
        borrow = a[i] < subtrahend;
        a[i] = (uint32_t)((uint64_t)a[i] - subtrahend);
    }

    return a;
}


std::array<uint32_t, 6> add_mod(const std::array<uint32_t, 6> &a, const std::array<uint32_t, 6> &b, const std::array<uint32_t, 6> &modulus)
{
    std::array<uint32_t, 6> result{};
    uint64_t carry = 0;
    for(uint32_t i = 0; i < result.size(); ++i)
    {
        uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        result[i] = (uint32_t)sum;
        carry = sum >> 32;
    }

    if(compare(result, modulus) >= 0)
        result = subtract(result, modulus);

    return result;
}


std::array<uint32_t, 6> subtract_mod(const std::array<uint32_t, 6> &a, const std::array<uint32_t, 6> &b, const std::array<uint32_t, 6> &modulus)
{
    if(compare(a, b) >= 0)
        return subtract(a, b);

    return subtract(modulus, subtract(b, a));
}


std::array<uint32_t, 6> multiply_mod(std::array<uint32_t, 6> a, const std::array<uint32_t, 6> &b, const std::array<uint32_t, 6> &modulus)
{
    std::array<uint32_t, 6> result{};
    for(uint32_t bit = 0; bit < 160; ++bit)
    {
        if((b[bit / 32] >> (bit % 32)) & 1)
            result = add_mod(result, a, modulus);
        a = add_mod(a, a, modulus);
    }

    return result;
}


std::array<uint32_t, 6> power_mod(const std::array<uint32_t, 6> &value, const std::array<uint32_t, 6> &exponent, const std::array<uint32_t, 6> &modulus)
{
    std::array<uint32_t, 6> result{ 1 };
    for(uint32_t bit = 160; bit-- > 0;)
    {
        result = multiply_mod(result, result, modulus);
        if((exponent[bit / 32] >> (bit % 32)) & 1)
            result = multiply_mod(result, value, modulus);
    }

    return result;
}


struct Point
{
    std::array<uint32_t, 6> x{};
    std::array<uint32_t, 6> y{};
    std::array<uint32_t, 6> z{};
};


Point point_double(const Point &point)
{
    if(is_zero(point.z))
        return Point{};

    auto delta = multiply_mod(point.z, point.z, P);
    auto gamma = multiply_mod(point.y, point.y, P);
    auto beta = multiply_mod(point.x, gamma, P);
    auto alpha = multiply_mod(subtract_mod(point.x, delta, P), add_mod(point.x, delta, P), P);
    alpha = add_mod(alpha, add_mod(alpha, alpha, P), P);

    auto beta4 = add_mod(beta, beta, P);
    beta4 = add_mod(beta4, beta4, P);
    auto beta8 = add_mod(beta4, beta4, P);
    auto x = subtract_mod(multiply_mod(alpha, alpha, P), beta8, P);
    auto yz = add_mod(point.y, point.z, P);
    auto z = subtract_mod(subtract_mod(multiply_mod(yz, yz, P), gamma, P), delta, P);
    auto gamma8 = multiply_mod(gamma, gamma, P);
    gamma8 = add_mod(gamma8, gamma8, P);
    gamma8 = add_mod(gamma8, gamma8, P);
    gamma8 = add_mod(gamma8, gamma8, P);
    auto y = subtract_mod(multiply_mod(alpha, subtract_mod(beta4, x, P), P), gamma8, P);
    return Point{ x, y, z };
}


Point point_add(const Point &point, const std::array<uint32_t, 6> &x, const std::array<uint32_t, 6> &y)
{
    if(is_zero(point.z))
        return { x, y, { 1 } };

    auto zz = multiply_mod(point.z, point.z, P);
    auto u = multiply_mod(x, zz, P);
    auto s = multiply_mod(y, multiply_mod(point.z, zz, P), P);
    auto h = subtract_mod(u, point.x, P);

    auto hh = multiply_mod(h, h, P);
    auto i = add_mod(hh, hh, P);
    i = add_mod(i, i, P);
    auto j = multiply_mod(h, i, P);
    auto r = subtract_mod(s, point.y, P);
    r = add_mod(r, r, P);
    auto v = multiply_mod(point.x, i, P);
    auto result_x = subtract_mod(subtract_mod(multiply_mod(r, r, P), j, P), add_mod(v, v, P), P);
    auto yj = multiply_mod(point.y, j, P);
    auto result_y = subtract_mod(multiply_mod(r, subtract_mod(v, result_x, P), P), add_mod(yj, yj, P), P);
    auto zh = add_mod(point.z, h, P);
    auto result_z = subtract_mod(subtract_mod(multiply_mod(zh, zh, P), zz, P), hh, P);
    return Point{ result_x, result_y, result_z };
}


std::array<uint32_t, 6> point_multiply(const std::array<uint32_t, 6> &scalar, const std::array<uint32_t, 6> &x, const std::array<uint32_t, 6> &y)
{
    Point result;
    for(uint32_t bit = 160; bit-- > 0;)
    {
        result = point_double(result);
        if((scalar[bit / 32] >> (bit % 32)) & 1)
            result = point_add(result, x, y);
    }

    auto inverse_z = power_mod(result.z, P_MINUS_2, P);
    return multiply_mod(result.x, multiply_mod(inverse_z, inverse_z, P), P);
}


std::array<uint32_t, 6> from_bytes(const uint8_t *bytes)
{
    std::array<uint32_t, 6> result{};
    for(uint32_t i = 0; i < 20; ++i)
        result[(19 - i) / 4] |= (uint32_t)bytes[i] << (((19 - i) % 4) * 8);
    return result;
}

}


export class ECDSA
{
public:
    static std::array<uint8_t, 40> sign(const std::array<uint8_t, 20> &private_key, const std::array<uint8_t, 20> &hash)
    {
        auto key = from_bytes(private_key.data());
        if(is_zero(key) || compare(key, N) >= 0)
            throw_line("invalid ECDSA private key");

        auto digest = from_bytes(hash.data());
        if(compare(digest, N) >= 0)
            digest = subtract(digest, N);

        std::random_device random;
        std::uniform_int_distribution<uint32_t> random_word;
        for(;;)
        {
            std::array<uint32_t, 6> k{};
            do
            {
                for(uint32_t i = 0; i < 5; ++i)
                    k[i] = random_word(random);
            } while(is_zero(k) || compare(k, N) >= 0);

            auto r = point_multiply(k, GX, GY);
            if(is_zero(r))
                continue;

            auto s = multiply_mod(power_mod(k, N_MINUS_2, N), add_mod(digest, multiply_mod(r, key, N), N), N);
            if(is_zero(s))
                continue;

            std::array<uint8_t, 40> signature;
            for(uint32_t i = 0; i < 20; ++i)
            {
                signature[i] = (uint8_t)(r[(19 - i) / 4] >> (((19 - i) % 4) * 8));
                signature[i + 20] = (uint8_t)(s[(19 - i) / 4] >> (((19 - i) % 4) * 8));
            }
            return signature;
        }
    }


    static std::array<uint8_t, 20> calculateKey(const std::array<uint8_t, 20> &private_key, const std::array<uint8_t, 40> &public_key)
    {
        auto key = from_bytes(private_key.data());
        if(is_zero(key) || compare(key, N) >= 0)
            throw_line("invalid ECDSA private key");

        auto x = from_bytes(public_key.data());
        auto y = from_bytes(public_key.data() + 20);
        if(compare(x, P) >= 0 || compare(y, P) >= 0)
            throw_line("invalid ECDSA public key");

        auto rhs = multiply_mod(multiply_mod(x, x, P), x, P);
        rhs = add_mod(subtract_mod(rhs, add_mod(x, add_mod(x, x, P), P), P), B, P);
        if(compare(multiply_mod(y, y, P), rhs) != 0)
            throw_line("invalid ECDSA public key");

        auto shared_x = point_multiply(key, x, y);
        std::array<uint8_t, 20> shared_key;
        for(uint32_t i = 0; i < shared_key.size(); ++i)
            shared_key[i] = (uint8_t)(shared_x[(19 - i) / 4] >> (((19 - i) % 4) * 8));
        return shared_key;
    }
};

}
