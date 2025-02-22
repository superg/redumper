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
};

export template<typename T, typename U>
const Range<T, U> *find_range(const std::vector<Range<T, U>> &ranges, T number)
{
    auto it = std::upper_bound(ranges.begin(), ranges.end(), number, [](T value, const Range<T, U> &range) { return value < range.start; });

    if(it != ranges.begin())
    {
        --it;
        if(it->contains(number))
        {
            return &(*it);
        }
    }

    return nullptr;
}

}
