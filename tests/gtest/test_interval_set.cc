#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <utility>
#include <vector>

import interval_set;

using namespace gpsxre;

using IS = IntervalSet<int32_t>;
using Interval = IS::Interval;


// ============================================================
// add
// ============================================================

TEST(IntervalSetAdd, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 5, 6 }
    }));
}

TEST(IntervalSetAdd, SingleRange)
{
    IS s;
    s.add(2, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 2, 5 }
    }));
}

TEST(IntervalSetAdd, EmptyRange)
{
    IS s;
    s.add(5, 5);
    ASSERT_TRUE(s.ranges().empty());
}

TEST(IntervalSetAdd, InvertedRange)
{
    IS s;
    s.add(5, 2);
    ASSERT_TRUE(s.ranges().empty());
}

TEST(IntervalSetAdd, NonOverlapping)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 3 },
                              { 5, 8 }
    }));
}

TEST(IntervalSetAdd, AdjacentMerge)
{
    IS s;
    s.add(0, 3);
    s.add(3, 6);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 6 }
    }));
}

TEST(IntervalSetAdd, AdjacentMergeReverse)
{
    IS s;
    s.add(3, 6);
    s.add(0, 3);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 6 }
    }));
}

TEST(IntervalSetAdd, OverlappingMerge)
{
    IS s;
    s.add(0, 5);
    s.add(3, 8);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 8 }
    }));
}

TEST(IntervalSetAdd, OverlappingMergeReverse)
{
    IS s;
    s.add(3, 8);
    s.add(0, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 8 }
    }));
}

TEST(IntervalSetAdd, ContainedWithin)
{
    IS s;
    s.add(0, 10);
    s.add(3, 7);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 10 }
    }));
}

TEST(IntervalSetAdd, Superset)
{
    IS s;
    s.add(3, 7);
    s.add(0, 10);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 10 }
    }));
}

TEST(IntervalSetAdd, MergeMultiple)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 13);
    s.add(2, 11);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 13 }
    }));
}

TEST(IntervalSetAdd, MergeAllAdjacent)
{
    IS s;
    s.add(0, 2);
    s.add(4, 6);
    s.add(2, 4);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 6 }
    }));
}

TEST(IntervalSetAdd, DuplicateAdd)
{
    IS s;
    s.add(0, 5);
    s.add(0, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 5 }
    }));
}

TEST(IntervalSetAdd, InsertInMiddle)
{
    IS s;
    s.add(0, 2);
    s.add(6, 8);
    s.add(3, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 2 },
                              { 3, 5 },
                              { 6, 8 }
    }));
}

TEST(IntervalSetAdd, SingleValues)
{
    IS s;
    s.add(1);
    s.add(3);
    s.add(5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 1, 2 },
                              { 3, 4 },
                              { 5, 6 }
    }));
}

TEST(IntervalSetAdd, SingleValuesMerge)
{
    IS s;
    s.add(1);
    s.add(2);
    s.add(3);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 1, 4 }
    }));
}

TEST(IntervalSetAdd, NegativeValues)
{
    IS s;
    s.add(-5, -2);
    s.add(-8, -4);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { -8, -2 }
    }));
}

TEST(IntervalSetAdd, NegativeToPositive)
{
    IS s;
    s.add(-3, 3);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { -3, 3 }
    }));
}


// ============================================================
// remove
// ============================================================

TEST(IntervalSetRemove, SingleValue)
{
    IS s;
    s.add(0, 10);
    s.remove(5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 5  },
                              { 6, 10 }
    }));
}

TEST(IntervalSetRemove, RangeFromMiddle)
{
    IS s;
    s.add(0, 10);
    s.remove(3, 7);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 3  },
                              { 7, 10 }
    }));
}

TEST(IntervalSetRemove, EntireRange)
{
    IS s;
    s.add(0, 10);
    s.remove(0, 10);
    ASSERT_TRUE(s.ranges().empty());
}

TEST(IntervalSetRemove, Superset)
{
    IS s;
    s.add(2, 8);
    s.remove(0, 10);
    ASSERT_TRUE(s.ranges().empty());
}

TEST(IntervalSetRemove, FromStart)
{
    IS s;
    s.add(0, 10);
    s.remove(0, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 5, 10 }
    }));
}

TEST(IntervalSetRemove, FromEnd)
{
    IS s;
    s.add(0, 10);
    s.remove(5, 10);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 5 }
    }));
}

TEST(IntervalSetRemove, NonOverlapping)
{
    IS s;
    s.add(0, 5);
    s.remove(6, 10);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 5 }
    }));
}

TEST(IntervalSetRemove, EmptySet)
{
    IS s;
    s.remove(0, 5);
    ASSERT_TRUE(s.ranges().empty());
}

TEST(IntervalSetRemove, EmptyRange)
{
    IS s;
    s.add(0, 10);
    s.remove(5, 5);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 10 }
    }));
}

TEST(IntervalSetRemove, AcrossMultipleIntervals)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 13);
    s.remove(2, 11);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0,  2  },
                              { 11, 13 }
    }));
}

TEST(IntervalSetRemove, ExactInterval)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 13);
    s.remove(5, 8);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0,  3  },
                              { 10, 13 }
    }));
}

TEST(IntervalSetRemove, FirstValue)
{
    IS s;
    s.add(0, 5);
    s.remove(0);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 1, 5 }
    }));
}

TEST(IntervalSetRemove, LastValue)
{
    IS s;
    s.add(0, 5);
    s.remove(4);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 4 }
    }));
}


// ============================================================
// contains
// ============================================================

TEST(IntervalSetContains, InsideRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_TRUE(s.contains(0));
    ASSERT_TRUE(s.contains(5));
    ASSERT_TRUE(s.contains(9));
}

TEST(IntervalSetContains, OutsideRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_FALSE(s.contains(-1));
    ASSERT_FALSE(s.contains(10));
    ASSERT_FALSE(s.contains(100));
}

TEST(IntervalSetContains, EmptySet)
{
    IS s;
    ASSERT_FALSE(s.contains(0));
}

TEST(IntervalSetContains, Gap)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_TRUE(s.contains(2));
    ASSERT_FALSE(s.contains(3));
    ASSERT_FALSE(s.contains(4));
    ASSERT_TRUE(s.contains(5));
}

TEST(IntervalSetContains, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_FALSE(s.contains(4));
    ASSERT_TRUE(s.contains(5));
    ASSERT_FALSE(s.contains(6));
}


// ============================================================
// next
// ============================================================

TEST(IntervalSetNext, WithinRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_EQ(s.next(0), 1);
    ASSERT_EQ(s.next(5), 6);
    ASSERT_EQ(s.next(8), 9);
}

TEST(IntervalSetNext, AtEndOfRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_EQ(s.next(9), std::nullopt);
}

TEST(IntervalSetNext, JumpToNextInterval)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.next(2), 5);
}

TEST(IntervalSetNext, InGap)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.next(3), 5);
    ASSERT_EQ(s.next(4), 5);
}

TEST(IntervalSetNext, BeforeAll)
{
    IS s;
    s.add(5, 8);
    ASSERT_EQ(s.next(-10), 5);
}

TEST(IntervalSetNext, AfterAll)
{
    IS s;
    s.add(0, 5);
    ASSERT_EQ(s.next(10), std::nullopt);
}

TEST(IntervalSetNext, EmptySet)
{
    IS s;
    ASSERT_EQ(s.next(0), std::nullopt);
}

TEST(IntervalSetNext, SingleElement)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.next(4), 5);
    ASSERT_EQ(s.next(5), std::nullopt);
}

TEST(IntervalSetNext, MultipleIntervals)
{
    IS s;
    s.add(0, 2);
    s.add(5, 7);
    s.add(10, 12);
    ASSERT_EQ(s.next(1), 5);
    ASSERT_EQ(s.next(6), 10);
    ASSERT_EQ(s.next(11), std::nullopt);
}


// ============================================================
// prev
// ============================================================

TEST(IntervalSetPrev, WithinRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_EQ(s.prev(9), 8);
    ASSERT_EQ(s.prev(5), 4);
    ASSERT_EQ(s.prev(1), 0);
}

TEST(IntervalSetPrev, AtStartOfRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_EQ(s.prev(0), std::nullopt);
}

TEST(IntervalSetPrev, JumpToPrevInterval)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.prev(5), 2);
}

TEST(IntervalSetPrev, InGap)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.prev(4), 2);
    ASSERT_EQ(s.prev(3), 2);
}

TEST(IntervalSetPrev, AfterAll)
{
    IS s;
    s.add(0, 5);
    ASSERT_EQ(s.prev(10), 4);
}

TEST(IntervalSetPrev, BeforeAll)
{
    IS s;
    s.add(5, 8);
    ASSERT_EQ(s.prev(0), std::nullopt);
}

TEST(IntervalSetPrev, EmptySet)
{
    IS s;
    ASSERT_EQ(s.prev(0), std::nullopt);
}

TEST(IntervalSetPrev, SingleElement)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.prev(6), 5);
    ASSERT_EQ(s.prev(5), std::nullopt);
}

TEST(IntervalSetPrev, MultipleIntervals)
{
    IS s;
    s.add(0, 2);
    s.add(5, 7);
    s.add(10, 12);
    ASSERT_EQ(s.prev(10), 6);
    ASSERT_EQ(s.prev(5), 1);
    ASSERT_EQ(s.prev(0), std::nullopt);
}


// ============================================================
// first
// ============================================================

TEST(IntervalSetFirst, NonEmpty)
{
    IS s;
    s.add(5, 10);
    ASSERT_EQ(s.first(), 5);
}

TEST(IntervalSetFirst, MultipleIntervals)
{
    IS s;
    s.add(5, 10);
    s.add(0, 3);
    ASSERT_EQ(s.first(), 0);
}

TEST(IntervalSetFirst, EmptySet)
{
    IS s;
    ASSERT_EQ(s.first(), std::nullopt);
}

TEST(IntervalSetFirst, Negative)
{
    IS s;
    s.add(-10, -5);
    ASSERT_EQ(s.first(), -10);
}


// ============================================================
// last
// ============================================================

TEST(IntervalSetLast, NonEmpty)
{
    IS s;
    s.add(5, 10);
    ASSERT_EQ(s.last(), 9);
}

TEST(IntervalSetLast, MultipleIntervals)
{
    IS s;
    s.add(0, 3);
    s.add(5, 10);
    ASSERT_EQ(s.last(), 9);
}

TEST(IntervalSetLast, EmptySet)
{
    IS s;
    ASSERT_EQ(s.last(), std::nullopt);
}

TEST(IntervalSetLast, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.last(), 5);
}


// ============================================================
// interval_end
// ============================================================

TEST(IntervalSetIntervalEnd, InsideRange)
{
    IS s;
    s.add(2, 8);
    ASSERT_EQ(s.interval_end(2), 8);
    ASSERT_EQ(s.interval_end(5), 8);
    ASSERT_EQ(s.interval_end(7), 8);
}

TEST(IntervalSetIntervalEnd, AtEndIsOutside)
{
    IS s;
    s.add(2, 8);
    ASSERT_EQ(s.interval_end(8), std::nullopt);
}

TEST(IntervalSetIntervalEnd, InGap)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.interval_end(3), std::nullopt);
    ASSERT_EQ(s.interval_end(4), std::nullopt);
}

TEST(IntervalSetIntervalEnd, BeforeAll)
{
    IS s;
    s.add(5, 10);
    ASSERT_EQ(s.interval_end(0), std::nullopt);
    ASSERT_EQ(s.interval_end(4), std::nullopt);
}

TEST(IntervalSetIntervalEnd, AfterAll)
{
    IS s;
    s.add(5, 10);
    ASSERT_EQ(s.interval_end(10), std::nullopt);
    ASSERT_EQ(s.interval_end(100), std::nullopt);
}

TEST(IntervalSetIntervalEnd, EmptySet)
{
    IS s;
    ASSERT_EQ(s.interval_end(0), std::nullopt);
}

TEST(IntervalSetIntervalEnd, MultipleIntervals)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 15);
    ASSERT_EQ(s.interval_end(0), 3);
    ASSERT_EQ(s.interval_end(2), 3);
    ASSERT_EQ(s.interval_end(5), 8);
    ASSERT_EQ(s.interval_end(7), 8);
    ASSERT_EQ(s.interval_end(10), 15);
    ASSERT_EQ(s.interval_end(14), 15);
}

TEST(IntervalSetIntervalEnd, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.interval_end(5), 6);
    ASSERT_EQ(s.interval_end(4), std::nullopt);
    ASSERT_EQ(s.interval_end(6), std::nullopt);
}

TEST(IntervalSetIntervalEnd, Negative)
{
    IS s;
    s.add(-10, -5);
    ASSERT_EQ(s.interval_end(-10), -5);
    ASSERT_EQ(s.interval_end(-7), -5);
    ASSERT_EQ(s.interval_end(-5), std::nullopt);
}


// ============================================================
// empty
// ============================================================

TEST(IntervalSetEmpty, Default)
{
    IS s;
    ASSERT_TRUE(s.empty());
}

TEST(IntervalSetEmpty, AfterAdd)
{
    IS s;
    s.add(0, 5);
    ASSERT_FALSE(s.empty());
}

TEST(IntervalSetEmpty, AfterAddAndRemoveAll)
{
    IS s;
    s.add(0, 5);
    s.remove(0, 5);
    ASSERT_TRUE(s.empty());
}


// ============================================================
// combined operations
// ============================================================

TEST(IntervalSetCombined, AddRemoveAdd)
{
    IS s;
    s.add(0, 10);
    s.remove(3, 7);
    s.add(5, 8);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 3  },
                              { 5, 10 }
    }));
}

TEST(IntervalSetCombined, BuildFromSingleValues)
{
    IS s;
    for(int i = 0; i < 10; ++i)
        s.add(i);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 10 }
    }));
}

TEST(IntervalSetCombined, BuildFromSingleValuesReverse)
{
    IS s;
    for(int i = 9; i >= 0; --i)
        s.add(i);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0, 10 }
    }));
}

TEST(IntervalSetCombined, RemoveFromMiddleNavigate)
{
    IS s;
    s.add(0, 20);
    s.remove(5, 10);
    s.remove(15, 18);

    // ranges: [0,5) [10,15) [18,20)
    ASSERT_EQ(s.first(), 0);
    ASSERT_EQ(s.last(), 19);

    ASSERT_EQ(s.next(4), 10);
    ASSERT_EQ(s.next(14), 18);
    ASSERT_EQ(s.next(19), std::nullopt);

    ASSERT_EQ(s.prev(10), 4);
    ASSERT_EQ(s.prev(18), 14);
    ASSERT_EQ(s.prev(0), std::nullopt);
}

// ============================================================
// count
// ============================================================

TEST(IntervalSetCount, EmptySet)
{
    IS s;
    ASSERT_EQ(s.count(), 0);
}

TEST(IntervalSetCount, SingleRange)
{
    IS s;
    s.add(0, 10);
    ASSERT_EQ(s.count(), 10);
}

TEST(IntervalSetCount, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.count(), 1);
}

TEST(IntervalSetCount, MultipleRanges)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 13);
    ASSERT_EQ(s.count(), 9);
}

TEST(IntervalSetCount, AfterRemove)
{
    IS s;
    s.add(0, 10);
    s.remove(3, 7);
    ASSERT_EQ(s.count(), 6);
}

TEST(IntervalSetCount, NegativeRange)
{
    IS s;
    s.add(-5, 5);
    ASSERT_EQ(s.count(), 10);
}


// ============================================================
// index
// ============================================================

TEST(IntervalSetIndex, EmptySet)
{
    IS s;
    ASSERT_EQ(s.index(0), std::nullopt);
}

TEST(IntervalSetIndex, SingleRange)
{
    IS s;
    s.add(0, 5);
    ASSERT_EQ(s.index(0), 0);
    ASSERT_EQ(s.index(1), 1);
    ASSERT_EQ(s.index(4), 4);
}

TEST(IntervalSetIndex, OutsideRange)
{
    IS s;
    s.add(0, 5);
    ASSERT_EQ(s.index(-1), std::nullopt);
    ASSERT_EQ(s.index(5), std::nullopt);
    ASSERT_EQ(s.index(100), std::nullopt);
}

TEST(IntervalSetIndex, MultipleRanges)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    // values: 0,1,2, 5,6,7
    ASSERT_EQ(s.index(0), 0);
    ASSERT_EQ(s.index(2), 2);
    ASSERT_EQ(s.index(5), 3);
    ASSERT_EQ(s.index(7), 5);
}

TEST(IntervalSetIndex, InGap)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    ASSERT_EQ(s.index(3), std::nullopt);
    ASSERT_EQ(s.index(4), std::nullopt);
}

TEST(IntervalSetIndex, ThreeRanges)
{
    IS s;
    s.add(0, 2);
    s.add(5, 7);
    s.add(10, 12);
    // values: 0,1, 5,6, 10,11
    ASSERT_EQ(s.index(0), 0);
    ASSERT_EQ(s.index(1), 1);
    ASSERT_EQ(s.index(5), 2);
    ASSERT_EQ(s.index(6), 3);
    ASSERT_EQ(s.index(10), 4);
    ASSERT_EQ(s.index(11), 5);
}

TEST(IntervalSetIndex, SingleValue)
{
    IS s;
    s.add(5);
    ASSERT_EQ(s.index(5), 0);
    ASSERT_EQ(s.index(4), std::nullopt);
    ASSERT_EQ(s.index(6), std::nullopt);
}

TEST(IntervalSetIndex, NegativeValues)
{
    IS s;
    s.add(-3, 0);
    s.add(2, 5);
    // values: -3,-2,-1, 2,3,4
    ASSERT_EQ(s.index(-3), 0);
    ASSERT_EQ(s.index(-1), 2);
    ASSERT_EQ(s.index(2), 3);
    ASSERT_EQ(s.index(4), 5);
}

TEST(IntervalSetIndex, ConsistentWithCount)
{
    IS s;
    s.add(0, 3);
    s.add(5, 8);
    s.add(10, 12);
    // last valid index should be count() - 1
    ASSERT_EQ(s.index(11), s.count() - 1);
}


TEST(IntervalSetCombined, OverlappingAddsAndRemoves)
{
    IS s;
    s.add(0, 5);
    s.add(3, 8);
    s.add(10, 15);
    s.remove(6, 12);
    ASSERT_EQ(s.ranges(), (std::vector<Interval>{
                              { 0,  6  },
                              { 12, 15 }
    }));
}
