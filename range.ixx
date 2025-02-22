module;
#include <algorithm>
#include <cstdint>
#include <vector>

export module range;



namespace gpsxre
{

export template<typename T, typename U>
struct Range
{
    T start;
    T end;

    U data;

    bool contains(T value) const
    {
        return start <= value && value < end;
    }

    bool valid() const
    {
        return start < end;
    }

    bool operator<(const Range<T, U> &range) const
    {
        return start < range;
    }

    friend bool operator<(T value, const Range<T, U> &range)
    {
        return value < range.start;
    }
};

export template<typename T, typename U>
const Range<T, U> *find_range(const std::vector<Range<T, U>> &ranges, T number)
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

export template<typename T, typename U>
bool insert_range(std::vector<Range<T, U>> &ranges, Range<T, U> range)
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
