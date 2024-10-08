module;
#include <array>
#include <cassert>
#include <cstdint>

export module cd.subcode_rw;

import cd.cd;

namespace gpsxre
{

namespace GF64
{

// Binary extension Galois (finite) field GF(64), GF(2**6).
// This is used for the polynomial coefficients in R-W pack parity
// Reed-Solomon codes.
// ref <https://github.com/geky/gf256>,
//     <https://en.wikipedia.org/wiki/Finite_field>

const unsigned int K = 6;  // number of bits in binary representation
const unsigned int M = 63; // non-zero element count

// Elements of the field are polynomials in GF(2) with degree less
// than 6. The GF(2) field works naturally with bitwise operations,
// addition (and subtraction) is XOR, multiplication is AND.
// The coefficients are packed into the low 6 bits of a uint8_t,
// each bit i (1 << i) is the coefficient of X**i. Addition is XOR,
// multiplication by X**i is << i.
typedef uint8_t gf64;
typedef uint16_t gf64_product;

// Multiplication in GF(64) is polynomial multiplication, modulo the
// irreducible polynomial X**6+X+1. (If it was reducible then non-zero
// elements P1*P2=P would multiply to zero.)
const gf64_product POLY = 0x43;
// Generator element, polynomial X. Every non-zero element in GF(64) is some
// power of G (G**i), and often we will use elements in order from
// G**0 (1) to G**M. G_POW is the table version of this.
const gf64 G = 2;

static constexpr gf64 multiply(gf64 v1, gf64 v2)
{
    // Multiply by shift and add
    gf64_product p = 0;
    for(unsigned int i = 0; i < K; ++i)
    {
        if((v1 >> i) & 1)
        {
            // X**i has coefficient 1 in v1, add v2 * X**i
            p ^= ((gf64_product)v2) << i;
        }
    }
    // The resulting product may have degree >= K (up to K*2-1), GF(64) can
    // have degree at most K-1. Calculate the remainder from division by the
    // irreducible polynomial (degree K), resulting in degree at most K-1.
    for(unsigned int i = K * 2 - 1; i >= K; --i)
    {
        if((p >> i) & 1)
        {
            // p has degree i, subtract POLY * X**(i - K) (degree i), which
            // will reduce the degree to < i.
            p ^= POLY << (i - K);
        }
    }
    return p;
}

static constexpr gf64 reciprocal(gf64 v)
{
    // Compute powers of v until v**i = 1, the power r=v**i/v before that
    // is the reciprocal (multiplicative inverse) 1/v.
    // This is relatively inefficient but we are only using it once per element
    // at compile time.
    assert(v != 0);
    gf64 prev = 0;
    gf64 m = v;
    while(1)
    {
        prev = m;
        m = multiply(m, v);
        if(m == 1)
        {
            return prev;
        }
    }
}

// Precomputed multiplication table (64*64 = 4KiB)
static constexpr auto MULTIPLY = []()
{
    std::array<std::array<gf64, M + 1>, M + 1> table;

    for(gf64 j = 0; j < M + 1; ++j)
    {
        for(gf64 i = 0; i < M + 1; ++i)
        {
            table[j][i] = multiply(j, i);
        }
    }

    return table;
}();

// Precomputed reciprocal table (64 bytes)
static constexpr auto RECIPROCAL = []()
{
    std::array<gf64, M + 1> table;

    table[0] = 0;
    for(gf64 i = 1; i < M + 1; ++i)
    {
        table[i] = reciprocal(i);
    }

    return table;
}();

// Precomputed powers of G (64 bytes)
static constexpr auto G_POW = []()
{
    std::array<gf64, M + 1> table;

    gf64 x = 1;
    for(gf64 i = 0; i < M + 1; ++i)
    {
        table[i] = x;
        x = multiply(x, G);
    }

    return table;
}();

}

namespace SubcodeRwParityP
{

// R-W pack parity P, (24,20) Reed-Solomon (RS) code, BCH view
//
// The Parity P RS code considers the 6-bit symbols of the pack as coefficients
// of a polynomial in GF(64). There are 20 data symbols and 4 symbols added for
// parity, but for detecting errors all 24 are treated equally.
// Not to be confused with the P subchannel.

using GF64::G_POW;
using GF64::gf64;
using GF64::M;
using GF64::MULTIPLY;
using GF64::RECIPROCAL;

const unsigned int CODEWORD_SIZE = 24;
// one syndrome per parity symbol, this is the degree of the generator polynomial
const unsigned int SYNDROME_COUNT = 4;

// Swap between on-disc index order and ascending power order (where poly[i]
// is the coef of X**i)
unsigned int swap_index(unsigned int i)
{
    return CODEWORD_SIZE - 1 - i;
}

// Evaluate codeword polynomial at the first SYNDROME_COUNT powers of G.
std::array<gf64, SYNDROME_COUNT> eval_syndromes(const std::array<gf64, CODEWORD_SIZE> &swapped_codeword)
{
    std::array<gf64, SYNDROME_COUNT> syndromes;
    // Since we are evaluating at powers of G, we can keep around an array of
    // terms, summed together on each iteration and then multiplied by G**i for
    // the next one. This Chien search-style evluation appears to run faster
    // than Horner's method.
    // Indexes in `swapped_codeword` and `swapped_terms` are the on-disc order.
    std::array<gf64, CODEWORD_SIZE> swapped_terms = swapped_codeword;
    for(unsigned int j = 0; j < SYNDROME_COUNT; ++j)
    {
        gf64 total = swapped_terms[swap_index(0)];
        for(unsigned int i = 1; i < CODEWORD_SIZE; ++i)
        {
            unsigned int si = swap_index(i);
            total ^= swapped_terms[si];
            if(j + 1 < SYNDROME_COUNT)
            {
                swapped_terms[si] = MULTIPLY[G_POW[i]][swapped_terms[si]];
            }
        }
        syndromes[j] = total;
    }

    return syndromes;
}

// Calculate the error locator polynomial, return the number of errors
//
// Uses the Berlekamp-Massey algorithm
// ref <https://en.wikipedia.org/wiki/Berlekamp%E2%80%93Massey_algorithm>

unsigned int find_error_locator(const std::array<gf64, SYNDROME_COUNT> &syndromes, std::array<gf64, SYNDROME_COUNT + 1> &C)
{
    C = { 1, 0, 0, 0, 0 };                      // estimate of error locator polynomial
    std::array<gf64, SYNDROME_COUNT + 1> B = C; // last estimate
    gf64 b_recip = 1;                           // reciprocal of B's discrepancy
    unsigned int L = 0;                         // estimated number of errors

    for(unsigned int n = 0; n < SYNDROME_COUNT; ++n)
    {
        assert(C[0] == 1);
        gf64 d = syndromes[n];
        for(unsigned int j = 1; j < L + 1; ++j)
        {
            d ^= MULTIPLY[C[j]][syndromes[n - j]];
        }

        // Shift B to follow the syndromes corresponding to the last discrepancy
        assert(B[SYNDROME_COUNT] == 0);
        for(unsigned int i = SYNDROME_COUNT; i > 0; --i)
        {
            B[i] = B[i - 1];
        }
        B[0] = 0;

        if(d != 0)
        {
            gf64 scale = MULTIPLY[d][b_recip];
            if(2 * L <= n)
            {
                for(unsigned int i = 0; i < SYNDROME_COUNT + 1; ++i)
                {
                    gf64 t = C[i];
                    C[i] ^= MULTIPLY[scale][B[i]];
                    B[i] = t;
                }
                b_recip = RECIPROCAL[d];
                L = n + 1 - L;
            }
            else
            {
                for(unsigned int i = 0; i < SYNDROME_COUNT + 1; ++i)
                {
                    C[i] ^= MULTIPLY[scale][B[i]];
                }
            }
        }
    }

    return L;
}

// Return a bit field with bit i set if there is likely an error in
// swapped_codeword[i].
uint32_t locate_errors(const std::array<gf64, CODEWORD_SIZE> &swapped_codeword)
{
    // In unclear cases all CODEWORD_SIZE bits will be set.
    const uint32_t ALL_ERRORS = (UINT32_C(1) << CODEWORD_SIZE) - 1;

    auto syndromes = eval_syndromes(swapped_codeword);
    bool error = false;
    for(unsigned int i = 0; !error && i < SYNDROME_COUNT; ++i)
    {
        if(syndromes[i] != 0)
        {
            error = true;
        }
    }
    if(!error)
    {
        return 0;
    }

    std::array<gf64, SYNDROME_COUNT + 1> error_locator;
    unsigned int error_count = find_error_locator(syndromes, error_locator);

    if(error_count > SYNDROME_COUNT / 2)
    {
        // Exceeded design distance, errors were not reliably located.
        return ALL_ERRORS;
    }

    // Find roots of the error locator by brute force Chien search. This
    // evaluates the polynomial in the order given by the powers of generator G
    // so that fewer multiplies are required.
    // ref <https://en.wikipedia.org/wiki/Chien_search>
    // All roots are checked, instead of only those inside the codeword,
    // because extra roots indicate that errors were not reliably located.
    if(error_locator[0] == 0)
    {
        // This condition (root at 0) may not be possible, since there is no
        // G**-i == 0 the locator is invalid.
        return ALL_ERRORS;
    }

    uint32_t error_locations = 0;
    unsigned int root_count = 0;
    auto terms = error_locator; // terms evaluated at the current power of G, initialized to the coefs, G**0 = 1

    for(unsigned int i = 0; i < M; ++i)
    {
        gf64 total = terms[0];
        for(unsigned int j = 1; j < SYNDROME_COUNT + 1; ++j)
        {
            total ^= terms[j];
            if(i + 1 < M)
            {
                terms[j] = MULTIPLY[G_POW[j]][terms[j]];
            }
        }

        if(total == 0)
        {
            // A root at G**-i indicates an error at location i in the
            // codeword. Negative powers G**-i are equal to G**(M-i).
            unsigned int location = i == 0 ? 0 : M - i;
            if(location >= CODEWORD_SIZE || root_count == error_count)
            {
                // Too many roots, or roots beyond the codeword, therefore
                // errors were not reliably located.
                return ALL_ERRORS;
            }
            error_locations |= UINT32_C(1) << swap_index(location);
            root_count += 1;
        }
    }

    if(root_count != error_count)
    {
        // Wrong number of roots, errors were not reliably located.
        return ALL_ERRORS;
    }

    // TODO Attempt to correct errors. This may fail, giving one more chance
    // to detect that errors were not reliably located.

    return error_locations;
}

}

export const uint32_t CD_RW_PACK_SIZE = 24;

static constexpr uint32_t logical_to_interleaved_offset(int pack_i, int col)
{
    // Symbols 1, 2, and 3 are swapped (scrambled) with 18, 5, and 23.
    //
    // Technical aside: This scramble increases the distance between the first
    // 4 symbols, which are essential for interpreting the type of pack: these
    // give the pack mode, item (command type), and Q parity (a (4,2) RS code,
    // capable of correcting 1 symbol error).
    // With the convolutional interleaver (below) these would be evenly
    // separated on the disc by 24 frames. The swap separates them by a
    // minimum of 58 frames, which allows recovering the pack type from a much
    // larger burst error.
    // That said, this module is not concerned with Q parity, so we only need
    // this in order to undo the swap.
    switch(col)
    {
    case 1:
        col = 18;
        break;
    case 18:
        col = 1;
        break;

    case 2:
        col = 5;
        break;
    case 5:
        col = 2;
        break;

    case 3:
        col = 23;
        break;
    case 23:
        col = 3;
        break;
    }
    // Convolutional interleaver delay. This is from the perspective of the
    // encoder (logical -> interleaved); the decoder instead delays symbols
    // by the latency (7) minus delay. We have random access so we can use
    // this simpler approach, avoiding negative pack offsets.
    int delay = col & 7;
    return (pack_i + delay) * CD_RW_PACK_SIZE + col;
}

export const uint32_t CD_RW_PACK_CONTEXT_SECTORS = 5;

// This function checks that a sector contains no R-W packs with errors. R-W
// packs are spread over 3 sectors, so this takes a 5 sector context [-2..+2]
// in order to check all packs with any symbols in the center sector.
//
// If the parity check succeeds for all packs, the sector is considered valid.
//
// If the parity check can locate errors (up to 2 symbols per pack), then the
// center sector is only considered invalid if it contains the erroneous
// symbols.
//
// Otherwise all sectors containing the pack are considered invalid.
export bool valid_rw_packs(const std::array<uint8_t, CD_RW_PACK_CONTEXT_SECTORS * CD_SUBCODE_SIZE> &sector_subcode_rw_packs)
{
    // Deinterleave
    for(int pack_i = 0; pack_i < sector_subcode_rw_packs.size() / CD_RW_PACK_SIZE; ++pack_i)
    {
        std::array<uint8_t, CD_RW_PACK_SIZE> pack_data;

        bool touches_middle_sector = false;
        bool complete = true;
        for(int col = 0; col < CD_RW_PACK_SIZE; ++col)
        {
            int offset = logical_to_interleaved_offset(pack_i, col);

            if(offset >= sector_subcode_rw_packs.size())
            {
                complete = false;
                break;
            }
            if(offset >= CD_SUBCODE_SIZE * 2 && offset < CD_SUBCODE_SIZE * 3)
            {
                touches_middle_sector = true;
            }
            pack_data[col] = sector_subcode_rw_packs[offset] & 0x3f;
        }
        if(!complete || !touches_middle_sector)
        {
            continue;
        }

        uint32_t errors = SubcodeRwParityP::locate_errors(pack_data);

        if(errors)
        {
            for(int col = 0; col < CD_RW_PACK_SIZE; ++col)
            {
                uint32_t err_bit = (errors >> col) & 1;
                if(!err_bit)
                {
                    continue;
                }
                unsigned int error_sector = logical_to_interleaved_offset(pack_i, col) / CD_SUBCODE_SIZE;
                if(error_sector == 2)
                {
                    // found a bad symbol in the center sector
                    return false;
                }
            }
        }
    }

    return true;
}

}
