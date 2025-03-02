module;
#include <algorithm>
#include <cstdint>
#include <vector>

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
bool insert_range(std::vector<Range<T>> &ranges, Range<T> range)
{
    if(!range.valid())
        return false;

    auto it = std::lower_bound(ranges.begin(), ranges.end(), range);

    // overlap check
    if(it != ranges.end() && range.end > it->start)
        return false;
    if(it != ranges.begin() && (it - 1)->end > range.start)
        return false;

    ranges.insert(it, range);

    return true;
}

}
