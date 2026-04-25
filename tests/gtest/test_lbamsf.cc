#include <cstdint>
#include <gtest/gtest.h>

import cd.cd;

using namespace gpsxre;


struct LbaMsfCase
{
    MSF msf;
    int32_t lba;
};


class LbaMsf : public ::testing::TestWithParam<LbaMsfCase>
{
};


TEST_P(LbaMsf, MsfToLba)
{
    const auto &c = GetParam();
    EXPECT_EQ(MSF_to_LBA(c.msf), c.lba);
}


TEST_P(LbaMsf, LbaToMsf)
{
    const auto &c = GetParam();
    auto msf = LBA_to_MSF(c.lba);
    EXPECT_EQ(msf.m, c.msf.m);
    EXPECT_EQ(msf.s, c.msf.s);
    EXPECT_EQ(msf.f, c.msf.f);
}


TEST_P(LbaMsf, BcdMsfRoundTrip)
{
    const auto &c = GetParam();

    if(c.msf.m >= 160)
        GTEST_SKIP() << "BCD encoding does not cover MSF.m >= 160";

    auto bcdmsf = MSF_to_BCDMSF(c.msf);
    EXPECT_EQ(BCDMSF_to_LBA(bcdmsf), c.lba);
    auto roundtrip = LBA_to_BCDMSF(c.lba);
    EXPECT_EQ(roundtrip.m, bcdmsf.m);
    EXPECT_EQ(roundtrip.s, bcdmsf.s);
    EXPECT_EQ(roundtrip.f, bcdmsf.f);
}


INSTANTIATE_TEST_SUITE_P(Cases, LbaMsf,
    ::testing::Values(
        LbaMsfCase{
            { 0, 0, 0 },
            -150
},
        LbaMsfCase{ { 0, 0, 1 }, -149 }, LbaMsfCase{ { 0, 0, 73 }, -77 }, LbaMsfCase{ { 0, 0, 74 }, -76 }, LbaMsfCase{ { 0, 1, 0 }, -75 }, LbaMsfCase{ { 0, 2, 0 }, 0 },
        LbaMsfCase{ { 79, 59, 74 }, 359849 }, LbaMsfCase{ { 80, 0, 0 }, 359850 }, LbaMsfCase{ { 89, 59, 74 }, 404849 }, LbaMsfCase{ { 90, 0, 0 }, 404850 }, LbaMsfCase{ { 90, 0, 1 }, 404851 },
        LbaMsfCase{ { 90, 1, 0 }, 404925 }, LbaMsfCase{ { 99, 59, 74 }, 449849 }, LbaMsfCase{ { 100, 0, 0 }, 449850 }, LbaMsfCase{ { 160, 0, 0 }, -45150 }, LbaMsfCase{ { 160, 0, 1 }, -45149 },
        LbaMsfCase{ { 160, 1, 0 }, -45075 }, LbaMsfCase{ { 169, 59, 74 }, -151 }));


// ============================================================
// MSF_valid / BCDMSF_valid
// ============================================================

struct MsfValidCase
{
    MSF msf;
    bool valid;
};


class MsfValidity : public ::testing::TestWithParam<MsfValidCase>
{
};


TEST_P(MsfValidity, MsfValid)
{
    const auto &c = GetParam();
    EXPECT_EQ(MSF_valid(c.msf), c.valid);
}


INSTANTIATE_TEST_SUITE_P(Cases, MsfValidity,
    ::testing::Values(
        MsfValidCase{
            { 0, 0, 0 },
            true
},
        MsfValidCase{ { 169, 59, 74 }, true }, MsfValidCase{ { 100, 30, 50 }, true }, MsfValidCase{ { 170, 0, 0 }, false }, MsfValidCase{ { 0, 60, 0 }, false }, MsfValidCase{ { 0, 0, 75 }, false },
        MsfValidCase{ { 200, 70, 80 }, false }));


TEST(BcdMsfValid, ValidBcd)
{
    EXPECT_TRUE(BCDMSF_valid(MSF{ 0x00, 0x00, 0x00 }));
    EXPECT_TRUE(BCDMSF_valid(MSF{ 0x99, 0x59, 0x74 }));
}


TEST(BcdMsfValid, InvalidBcd)
{
    EXPECT_FALSE(BCDMSF_valid(MSF{ 0x00, 0x60, 0x00 }));
    EXPECT_FALSE(BCDMSF_valid(MSF{ 0x00, 0x00, 0x80 }));
}
