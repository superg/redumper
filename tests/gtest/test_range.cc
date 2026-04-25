#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

import range;

using namespace gpsxre;


using R = Range<int32_t>;


// ============================================================
// Range::contains / Range::valid
// ============================================================

TEST(Range, ContainsBoundaries)
{
    R r{ 10, 20 };
    EXPECT_TRUE(r.contains(10));
    EXPECT_TRUE(r.contains(15));
    EXPECT_TRUE(r.contains(19));
    EXPECT_FALSE(r.contains(20));
    EXPECT_FALSE(r.contains(9));
    EXPECT_FALSE(r.contains(100));
}


TEST(Range, Valid)
{
    EXPECT_TRUE((R{ 0, 1 }).valid());
    EXPECT_TRUE((R{ -10, 10 }).valid());
    EXPECT_FALSE((R{ 5, 5 }).valid());
    EXPECT_FALSE((R{ 5, 4 }).valid());
}


// ============================================================
// insert_range
// ============================================================

TEST(InsertRange, NonOverlapping)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });
    insert_range(ranges, R{ 50, 60 });
    ASSERT_EQ(ranges.size(), 3u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 20);
    EXPECT_EQ(ranges[1].start, 30);
    EXPECT_EQ(ranges[1].end, 40);
    EXPECT_EQ(ranges[2].start, 50);
    EXPECT_EQ(ranges[2].end, 60);
}


TEST(InsertRange, OverlappingMerge)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });
    insert_range(ranges, R{ 50, 60 });
    insert_range(ranges, R{ 15, 35 });
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 40);
    EXPECT_EQ(ranges[1].start, 50);
    EXPECT_EQ(ranges[1].end, 60);
}


TEST(InsertRange, CoverAll)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });
    insert_range(ranges, R{ 50, 60 });
    insert_range(ranges, R{ 5, 65 });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].start, 5);
    EXPECT_EQ(ranges[0].end, 65);
}


TEST(InsertRange, AdjacentMerge)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 20, 30 });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 30);
}


TEST(InsertRange, ContainedWithin)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 50 });
    insert_range(ranges, R{ 20, 30 });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 50);
}


TEST(InsertRange, AtStartNoOverlap)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 20, 30 });
    insert_range(ranges, R{ 40, 50 });
    insert_range(ranges, R{ 5, 10 });
    ASSERT_EQ(ranges.size(), 3u);
    EXPECT_EQ(ranges[0].start, 5);
    EXPECT_EQ(ranges[0].end, 10);
    EXPECT_EQ(ranges[1].start, 20);
    EXPECT_EQ(ranges[1].end, 30);
    EXPECT_EQ(ranges[2].start, 40);
    EXPECT_EQ(ranges[2].end, 50);
}


TEST(InsertRange, AtStartWithOverlap)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 20, 30 });
    insert_range(ranges, R{ 40, 50 });
    insert_range(ranges, R{ 5, 25 });
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].start, 5);
    EXPECT_EQ(ranges[0].end, 30);
    EXPECT_EQ(ranges[1].start, 40);
    EXPECT_EQ(ranges[1].end, 50);
}


TEST(InsertRange, AtEndWithOverlap)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });
    insert_range(ranges, R{ 35, 60 });
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 20);
    EXPECT_EQ(ranges[1].start, 30);
    EXPECT_EQ(ranges[1].end, 60);
}


TEST(InsertRange, ExactDuplicate)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 10, 20 });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].start, 10);
    EXPECT_EQ(ranges[0].end, 20);
}


TEST(InsertRange, EmptyThrows)
{
    std::vector<R> ranges;
    EXPECT_THROW(insert_range(ranges, R{ 5, 5 }), std::runtime_error);
}


TEST(InsertRange, InvertedThrows)
{
    std::vector<R> ranges;
    EXPECT_THROW(insert_range(ranges, R{ 10, 5 }), std::runtime_error);
}


// ============================================================
// find_range
// ============================================================

TEST(FindRange, Found)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });
    insert_range(ranges, R{ 50, 60 });

    auto r1 = find_range(ranges, 15);
    auto r2 = find_range(ranges, 35);
    auto r3 = find_range(ranges, 55);

    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->start, 10);
    EXPECT_EQ(r1->end, 20);

    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->start, 30);
    EXPECT_EQ(r2->end, 40);

    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(r3->start, 50);
    EXPECT_EQ(r3->end, 60);
}


TEST(FindRange, InGap)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    insert_range(ranges, R{ 30, 40 });

    EXPECT_EQ(find_range(ranges, 25), nullptr);
}


TEST(FindRange, EmptyVector)
{
    std::vector<R> ranges;
    EXPECT_EQ(find_range(ranges, 0), nullptr);
    EXPECT_EQ(find_range(ranges, 100), nullptr);
}


TEST(FindRange, BeforeAll)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    EXPECT_EQ(find_range(ranges, 5), nullptr);
}


TEST(FindRange, AfterAll)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });
    EXPECT_EQ(find_range(ranges, 25), nullptr);
}


TEST(FindRange, AtBoundaries)
{
    std::vector<R> ranges;
    insert_range(ranges, R{ 10, 20 });

    auto at_start = find_range(ranges, 10);
    ASSERT_NE(at_start, nullptr);
    EXPECT_EQ(at_start->start, 10);

    EXPECT_EQ(find_range(ranges, 20), nullptr);
}
