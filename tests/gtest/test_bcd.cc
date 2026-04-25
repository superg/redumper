#include <cstdint>
#include <gtest/gtest.h>

import cd.cd;

using namespace gpsxre;


struct BcdCase
{
    uint8_t encoded;
    uint8_t decoded;
};


class Bcd : public ::testing::TestWithParam<BcdCase>
{
};


TEST_P(Bcd, Decode)
{
    const auto &c = GetParam();
    EXPECT_EQ(bcd_decode(c.encoded), c.decoded);
}


TEST_P(Bcd, Encode)
{
    const auto &c = GetParam();
    EXPECT_EQ(bcd_encode(c.decoded), c.encoded);
}


TEST_P(Bcd, RoundTripDecodeEncode)
{
    const auto &c = GetParam();
    EXPECT_EQ(bcd_encode(bcd_decode(c.encoded)), c.encoded);
}


TEST_P(Bcd, RoundTripEncodeDecode)
{
    const auto &c = GetParam();
    EXPECT_EQ(bcd_decode(bcd_encode(c.decoded)), c.decoded);
}


INSTANTIATE_TEST_SUITE_P(Cases, Bcd,
    ::testing::Values(BcdCase{ 0x00, 0 }, BcdCase{ 0x01, 1 }, BcdCase{ 0x09, 9 }, BcdCase{ 0x10, 10 }, BcdCase{ 0x11, 11 }, BcdCase{ 0x15, 15 }, BcdCase{ 0x19, 19 }, BcdCase{ 0x55, 55 },
        BcdCase{ 0x99, 99 }, BcdCase{ 0xA0, 100 }, BcdCase{ 0xA1, 101 }, BcdCase{ 0xA6, 106 }, BcdCase{ 0xA9, 109 }, BcdCase{ 0xB0, 110 }, BcdCase{ 0xF0, 150 }, BcdCase{ 0xF9, 159 }));
