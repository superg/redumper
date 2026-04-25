#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

import utils.misc;

using namespace gpsxre;


struct BitCopyCase
{
    std::string name;
    std::vector<uint8_t> src;
    std::size_t src_offset;
    std::vector<uint8_t> dst_initial;
    std::size_t dst_offset;
    std::size_t size;
    std::vector<uint8_t> expected;
};


static void PrintTo(const BitCopyCase &c, std::ostream *os)
{
    *os << c.name;
}


class BitCopy : public ::testing::TestWithParam<BitCopyCase>
{
};


TEST_P(BitCopy, Copies)
{
    const auto &c = GetParam();
    SCOPED_TRACE(c.name);

    auto dst = c.dst_initial;
    bit_copy(dst.data(), c.dst_offset, c.src.data(), c.src_offset, c.size);

    EXPECT_EQ(dst, c.expected);
}


// clang-format off
INSTANTIATE_TEST_SUITE_P(Cases, BitCopy,
    ::testing::Values(
    BitCopyCase{
        "aligned_full_byte",
        { 0xAB }, 0,
        { 0x00 }, 0, 8,
        { 0xAB }
    },
    BitCopyCase{
        "aligned_low_nibble_in_byte",
        { 0xAB }, 4,
        { 0xC0 }, 4, 4,
        { 0xCB }
    },
    BitCopyCase{
        "aligned_spanning_three_bytes_partial",
        { 0xAB, 0xCD, 0xEF }, 4,
        { 0xC0, 0x00, 0x00 }, 4, 16,
        { 0xCB, 0xCD, 0xE0 }
    },
    BitCopyCase{
        "aligned_three_full_bytes",
        { 0x12, 0x34, 0x56 }, 0,
        { 0x00, 0x00, 0x00 }, 0, 24,
        { 0x12, 0x34, 0x56 }
    },
    BitCopyCase{
        "unaligned_src_lt_dst_single_byte_split",
        { 0xFF }, 0,
        { 0x00, 0x00 }, 4, 8,
        { 0x0F, 0xF0 }
    },
    BitCopyCase{
        "unaligned_src_gt_dst_single_byte_split",
        { 0x00, 0xFF }, 4,
        { 0xFF, 0xFF }, 0, 8,
        { 0x0F, 0xFF }
    },
    BitCopyCase{
        "unaligned_src_lt_dst_spanning_three_bytes",
        { 0xAB, 0xCD, 0xEF }, 0,
        { 0x00, 0x00, 0x00, 0x00 }, 4, 20,
        { 0x0A, 0xBC, 0xDE, 0x00 }
    },
    BitCopyCase{
        "single_bit_aligned",
        { 0x80 }, 0,
        { 0x00 }, 0, 1,
        { 0x80 }
    },
    BitCopyCase{
        "single_bit_aligned_offset_3",
        { 0xFF }, 3,
        { 0x00 }, 3, 1,
        { 0x10 }
    },
    BitCopyCase{
        "preserves_unaffected_bits_around_destination",
        { 0xF0 }, 0,
        { 0xAA, 0xAA }, 4, 8,
        { 0xAF, 0x0A }
    }),
    [](const ::testing::TestParamInfo<BitCopyCase> &info) { return info.param.name; });
// clang-format on
