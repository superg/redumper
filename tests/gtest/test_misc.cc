#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

import utils.misc;

using namespace gpsxre;


// ============================================================
// is_zeroed
// ============================================================

TEST(IsZeroed, Empty)
{
    uint8_t data[1] = { 0xFF };
    EXPECT_TRUE(is_zeroed(data, 0));
}


TEST(IsZeroed, AllZeros)
{
    uint8_t data[16] = {};
    EXPECT_TRUE(is_zeroed(data, sizeof(data)));
}


TEST(IsZeroed, FirstByteNonZero)
{
    uint8_t data[16] = {};
    data[0] = 1;
    EXPECT_FALSE(is_zeroed(data, sizeof(data)));
}


TEST(IsZeroed, LastByteNonZero)
{
    uint8_t data[16] = {};
    data[15] = 1;
    EXPECT_FALSE(is_zeroed(data, sizeof(data)));
}


TEST(IsZeroed, Uint32Buffer)
{
    uint32_t data[4] = { 0, 0, 0, 0 };
    EXPECT_TRUE(is_zeroed(data, 4));
    data[2] = 1;
    EXPECT_FALSE(is_zeroed(data, 4));
}


// ============================================================
// bits_reflect
// ============================================================

TEST(BitsReflect, Uint8)
{
    EXPECT_EQ(bits_reflect<uint8_t>(0x00), 0x00);
    EXPECT_EQ(bits_reflect<uint8_t>(0xFF), 0xFF);
    EXPECT_EQ(bits_reflect<uint8_t>(0x80), 0x01);
    EXPECT_EQ(bits_reflect<uint8_t>(0x01), 0x80);
    EXPECT_EQ(bits_reflect<uint8_t>(0x12), 0x48);
    EXPECT_EQ(bits_reflect<uint8_t>(0xA5), 0xA5);
}


TEST(BitsReflect, Uint16)
{
    EXPECT_EQ(bits_reflect<uint16_t>(0x0000), 0x0000);
    EXPECT_EQ(bits_reflect<uint16_t>(0xFFFF), 0xFFFF);
    EXPECT_EQ(bits_reflect<uint16_t>(0x8000), 0x0001);
    EXPECT_EQ(bits_reflect<uint16_t>(0x0001), 0x8000);
    EXPECT_EQ(bits_reflect<uint16_t>(0x1234), 0x2C48);
}


TEST(BitsReflect, DoubleReflectIsIdentity)
{
    for(uint16_t v : { (uint16_t)0x0001, (uint16_t)0x1234, (uint16_t)0xABCD, (uint16_t)0xFEDC })
        EXPECT_EQ(bits_reflect<uint16_t>(bits_reflect<uint16_t>(v)), v);
}


// ============================================================
// sign_extend
// ============================================================

TEST(SignExtend, EightBitNegative)
{
    EXPECT_EQ((sign_extend<8, int32_t>(0xFF)), -1);
    EXPECT_EQ((sign_extend<8, int32_t>(0x80)), -128);
    EXPECT_EQ((sign_extend<8, int32_t>(0x81)), -127);
}


TEST(SignExtend, EightBitPositive)
{
    EXPECT_EQ((sign_extend<8, int32_t>(0x00)), 0);
    EXPECT_EQ((sign_extend<8, int32_t>(0x01)), 1);
    EXPECT_EQ((sign_extend<8, int32_t>(0x7F)), 127);
}


TEST(SignExtend, FourBit)
{
    EXPECT_EQ((sign_extend<4, int32_t>(0x0)), 0);
    EXPECT_EQ((sign_extend<4, int32_t>(0x7)), 7);
    EXPECT_EQ((sign_extend<4, int32_t>(0x8)), -8);
    EXPECT_EQ((sign_extend<4, int32_t>(0xF)), -1);
}


TEST(SignExtend, IgnoresExtraBits)
{
    EXPECT_EQ((sign_extend<8, int32_t>(0xFFFF)), -1);
    EXPECT_EQ((sign_extend<4, int32_t>(0xFFF8)), -8);
}


// ============================================================
// inside_range
// ============================================================

TEST(InsideRange, EmptyVector)
{
    std::vector<std::pair<int32_t, int32_t>> ranges;
    EXPECT_EQ(inside_range(0, ranges), nullptr);
}


TEST(InsideRange, FoundInFirst)
{
    std::vector<std::pair<int32_t, int32_t>> ranges = {
        { 0,  10 },
        { 20, 30 }
    };
    auto r = inside_range(5, ranges);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->first, 0);
    EXPECT_EQ(r->second, 10);
}


TEST(InsideRange, FoundInMiddle)
{
    std::vector<std::pair<int32_t, int32_t>> ranges = {
        { 0,  10 },
        { 20, 30 },
        { 40, 50 }
    };
    auto r = inside_range(25, ranges);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->first, 20);
    EXPECT_EQ(r->second, 30);
}


TEST(InsideRange, BoundariesInclusiveExclusive)
{
    std::vector<std::pair<int32_t, int32_t>> ranges = {
        { 10, 20 }
    };
    EXPECT_NE(inside_range(10, ranges), nullptr); // start inclusive
    EXPECT_EQ(inside_range(20, ranges), nullptr); // end exclusive
}


TEST(InsideRange, NotFound)
{
    std::vector<std::pair<int32_t, int32_t>> ranges = {
        { 0,  10 },
        { 20, 30 }
    };
    EXPECT_EQ(inside_range(15, ranges), nullptr);
    EXPECT_EQ(inside_range(-5, ranges), nullptr);
    EXPECT_EQ(inside_range(100, ranges), nullptr);
}


// ============================================================
// round_up_pow2
// ============================================================

TEST(RoundUpPow2, Zero)
{
    EXPECT_EQ(round_up_pow2<uint32_t>(0u, 16u), 0u);
}


TEST(RoundUpPow2, AlreadyAligned)
{
    EXPECT_EQ(round_up_pow2<uint32_t>(16u, 16u), 16u);
    EXPECT_EQ(round_up_pow2<uint32_t>(32u, 16u), 32u);
    EXPECT_EQ(round_up_pow2<uint32_t>(8u, 8u), 8u);
}


TEST(RoundUpPow2, Unaligned)
{
    EXPECT_EQ(round_up_pow2<uint32_t>(1u, 16u), 16u);
    EXPECT_EQ(round_up_pow2<uint32_t>(15u, 16u), 16u);
    EXPECT_EQ(round_up_pow2<uint32_t>(17u, 16u), 32u);
    EXPECT_EQ(round_up_pow2<uint32_t>(31u, 16u), 32u);
}


TEST(RoundUpPow2, LargeMultiple)
{
    EXPECT_EQ(round_up_pow2<uint32_t>(1u, 4096u), 4096u);
    EXPECT_EQ(round_up_pow2<uint32_t>(4096u, 4096u), 4096u);
    EXPECT_EQ(round_up_pow2<uint32_t>(4097u, 4096u), 8192u);
}
