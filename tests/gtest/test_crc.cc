#include <cstdint>
#include <gtest/gtest.h>
#include <string>

import cd.edc;
import crc.crc;
import crc.crc16_gsm;
import crc.crc32;
import dvd.edc;

using namespace gpsxre;


static const std::string CHECK_STRING = "123456789";


// ============================================================
// reference check values
// ============================================================

TEST(Crc16Gsm, Check)
{
    auto v = CRC16_GSM().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    EXPECT_EQ(v, 0xCE3Cu);
}


TEST(Crc32, Check)
{
    auto v = CRC32().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    EXPECT_EQ(v, 0xCBF43926u);
}


TEST(Edc, Check)
{
    auto v = EDC().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    EXPECT_EQ(v, 0x6EC2EDC4u);
}


TEST(DvdEdc, Check)
{
    auto v = DVD_EDC().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    EXPECT_EQ(v, 0xB27CE117u);
}


TEST(CrcReciprocal, Match)
{
    auto normal = CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, false>().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    auto reciprocal = CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, true>().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();
    EXPECT_EQ(normal, reciprocal);
}


// ============================================================
// generic CRC template invariants
// ============================================================

TEST(Crc32, ChainedUpdate)
{
    auto whole = CRC32().update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length()).final();

    auto split = CRC32().update((const uint8_t *)CHECK_STRING.data(), 4).update((const uint8_t *)CHECK_STRING.data() + 4, CHECK_STRING.length() - 4).final();

    EXPECT_EQ(split, whole);
    EXPECT_EQ(whole, 0xCBF43926u);
}


TEST(Crc32, Reset)
{
    CRC32 c;
    c.update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length());
    auto first = c.final();

    c.reset();
    c.update((const uint8_t *)CHECK_STRING.data(), CHECK_STRING.length());
    auto second = c.final();

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, 0xCBF43926u);
}


TEST(Crc32, EmptyInput)
{
    EXPECT_EQ(CRC32().final(), 0u);
}


TEST(Crc16Gsm, EmptyInput)
{
    EXPECT_EQ(CRC16_GSM().final(), 0xFFFFu);
}


TEST(Edc, EmptyInput)
{
    EXPECT_EQ(EDC().final(), 0u);
}
