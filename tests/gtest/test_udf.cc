#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <optional>

import filesystem.udf_size;

using namespace gpsxre;


TEST(UDF, VolumeSectorsCountIncludesReserveVDSAndTrailingAVDP)
{
    constexpr uint32_t sector_size = 2048;
    constexpr uint32_t partition_start = 277;
    constexpr uint32_t partition_length = 23728682;
    EXPECT_EQ(udf::get_volume_sectors_count(partition_start + partition_length, 23728959, 32768, sector_size), 23728976);
}


TEST(UDF, VolumeSectorsCountUsesPartitionEndWithoutReserveVDS)
{
    EXPECT_EQ(udf::get_volume_sectors_count(1000, 0, 0, 2048), 1001);
}


TEST(UDF, VolumeSectorsCountUsesLargestMetadataEnd)
{
    EXPECT_EQ(udf::get_volume_sectors_count(1000, 500, 2048, 2048), 1001);
    EXPECT_EQ(udf::get_volume_sectors_count(900, 1000, 2049, 2048), 1003);
}


TEST(UDF, VolumeSectorsCountRejectsInvalidOrOverflowingValues)
{
    EXPECT_EQ(udf::get_volume_sectors_count(0, 0, 0, 2048), std::nullopt);
    EXPECT_EQ(udf::get_volume_sectors_count(1000, 0, 0, 0), std::nullopt);
    EXPECT_EQ(udf::get_volume_sectors_count(1000, std::numeric_limits<uint32_t>::max(), 2048, 2048), std::nullopt);
}
