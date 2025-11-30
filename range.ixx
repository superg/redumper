module;
#include <algorithm>
#include <cstdint>
#include <vector>
#include "throw_line.hh"

export module range;



namespace gpsxre
{

export template<typename T>
struct Range
{
    T start;
    T end;

    bool contains(T value) const
    {
        return start <= value && value < end;
    }

    bool valid() const
    {
        return start < end;
    }

    bool operator<(const Range<T> &range) const
    {
        return start < range;
    }

    friend bool operator<(T value, const Range<T> &range)
    {
        return value < range.start;
    }
};

export template<typename T>
const Range<T> *find_range(const std::vector<Range<T>> &ranges, T number)
{
    auto it = std::upper_bound(ranges.begin(), ranges.end(), number);
    if(it != ranges.begin())
    {
        --it;
        if(it->contains(number))
            return &(*it);
    }

    return nullptr;
}

export template<typename T>
void insert_range(std::vector<Range<T>> &ranges, Range<T> range)
{
    if(!range.valid())
        throw_line("insertion failed, invalid range configuration: [{}, {})", range.start, range.end);

    auto it = std::lower_bound(ranges.begin(), ranges.end(), range);

    // find all ranges that overlap with the new range
    auto first_overlap = it;
    while(first_overlap != ranges.begin() && (first_overlap - 1)->end >= range.start)
        --first_overlap;

    auto last_overlap = it;
    while(last_overlap != ranges.end() && last_overlap->start <= range.end)
        ++last_overlap;

    // merge all overlapping ranges
    if(first_overlap != last_overlap)
    {
        range.start = std::min(range.start, first_overlap->start);
        range.end = std::max(range.end, (last_overlap - 1)->end);

        auto insertion_index = first_overlap - ranges.begin();
        ranges.erase(first_overlap, last_overlap);
        it = ranges.begin() + insertion_index;
    }

    ranges.insert(it, range);
}

}
