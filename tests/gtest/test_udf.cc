#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <optional>

import filesystem.udf_size;

using namespace gpsxre;


TEST(UDF, TrailingAVDPLbaFollowsReserveVDS)
{
    constexpr uint32_t sector_size = 2048;
    constexpr uint32_t partition_start = 277;
    constexpr uint32_t partition_length = 23728682;
    auto trailing_avdp_lba = udf::get_trailing_avdp_lba(partition_start + partition_length, 23728959, 32768, sector_size);

    ASSERT_EQ(trailing_avdp_lba, 23728975);
    EXPECT_EQ(*trailing_avdp_lba + 1, 23728976);
}


TEST(UDF, TrailingAVDPLbaUsesPartitionEndWithoutReserveVDS)
{
    EXPECT_EQ(udf::get_trailing_avdp_lba(1000, 0, 0, 2048), 1000);
}


TEST(UDF, TrailingAVDPLbaUsesLargestMetadataEnd)
{
    EXPECT_EQ(udf::get_trailing_avdp_lba(1000, 500, 2048, 2048), 1000);
    EXPECT_EQ(udf::get_trailing_avdp_lba(900, 1000, 2049, 2048), 1002);
}


TEST(UDF, TrailingAVDPLbaRejectsInvalidOrOverflowingValues)
{
    EXPECT_EQ(udf::get_trailing_avdp_lba(0, 0, 0, 2048), std::nullopt);
    EXPECT_EQ(udf::get_trailing_avdp_lba(1000, 0, 0, 0), std::nullopt);
    EXPECT_EQ(udf::get_trailing_avdp_lba(1000, std::numeric_limits<uint32_t>::max(), 2048, 2048), std::nullopt);
}


TEST(UDF, TrailingAVDPSearchIsBounded)
{
    EXPECT_FALSE(udf::is_trailing_avdp_search_lba(999, 1000));
    EXPECT_TRUE(udf::is_trailing_avdp_search_lba(1000, 1000));
    EXPECT_TRUE(udf::is_trailing_avdp_search_lba(1255, 1000));
    EXPECT_FALSE(udf::is_trailing_avdp_search_lba(1256, 1000));
}
