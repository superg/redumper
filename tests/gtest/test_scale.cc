#include <cstdint>
#include <gtest/gtest.h>

import utils.misc;

using namespace gpsxre;


struct ScaleCase
{
    int32_t value;
    uint32_t multiple;
    int32_t scale_up_expected;
    int32_t scale_down_expected;
};


class Scale : public ::testing::TestWithParam<ScaleCase>
{
};


TEST_P(Scale, Up)
{
    const auto &c = GetParam();
    EXPECT_EQ(scale_up(c.value, c.multiple), c.scale_up_expected);
}


TEST_P(Scale, Down)
{
    const auto &c = GetParam();
    EXPECT_EQ(scale_down(c.value, c.multiple), c.scale_down_expected);
}


TEST_P(Scale, RoundUp)
{
    const auto &c = GetParam();
    EXPECT_EQ(round_up(c.value, c.multiple), c.scale_up_expected * (int32_t)c.multiple);
}


TEST_P(Scale, RoundDown)
{
    const auto &c = GetParam();
    EXPECT_EQ(round_down(c.value, c.multiple), c.scale_down_expected * (int32_t)c.multiple);
}


INSTANTIATE_TEST_SUITE_P(Cases, Scale,
    ::testing::Values(
        // positive
        ScaleCase{ 0, 16, 0, 0 }, ScaleCase{ 1, 16, 1, 0 }, ScaleCase{ 15, 16, 1, 0 }, ScaleCase{ 16, 16, 1, 1 }, ScaleCase{ 17, 16, 2, 1 }, ScaleCase{ 20, 16, 2, 1 }, ScaleCase{ 32, 16, 2, 2 },
        ScaleCase{ 33, 16, 3, 2 },

        // negative
        ScaleCase{ -1, 16, -1, 0 }, ScaleCase{ -15, 16, -1, 0 }, ScaleCase{ -16, 16, -1, -1 }, ScaleCase{ -17, 16, -2, -1 }, ScaleCase{ -20, 16, -2, -1 }, ScaleCase{ -32, 16, -2, -2 },
        ScaleCase{ -33, 16, -3, -2 },

        // alternate divisor
        ScaleCase{ 7, 8, 1, 0 }, ScaleCase{ 8, 8, 1, 1 }, ScaleCase{ 100, 10, 10, 10 }));
