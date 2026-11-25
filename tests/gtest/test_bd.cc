#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>



import bd;

using namespace gpsxre;


TEST(BD, LBAToPSN)
{
    // DI of a three layer BD-ROM (redump 134225), every layer starts at its own PSN
    const std::vector<std::pair<uint32_t, uint32_t>> layers = {
        { 0x100000,  16292864 },
        { 0x2F76400, 16292864 },
        { 0x4100000, 14804384 }
    };

    EXPECT_EQ(bd::LBA_to_PSN(bd::LBA_START, layers), 0u);
    EXPECT_EQ(bd::LBA_to_PSN(16292863, layers), 0x100000u + 16292863);
    EXPECT_EQ(bd::LBA_to_PSN(16292864, layers), 0x2F76400u);
    EXPECT_EQ(bd::LBA_to_PSN(32585728 + 4321, layers), 0x4100000u + 4321);

    // without layers PSN = LBA + 0x100000
    EXPECT_EQ(bd::LBA_to_PSN(16292864, std::vector<std::pair<uint32_t, uint32_t>>()), 0x100000u + 16292864);
}
