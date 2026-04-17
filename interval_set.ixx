module;
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

export module interval_set;



namespace gpsxre
{

export template<typename T>
class IntervalSet
{
public:
    using Interval = std::pair<T, T>;

    void add(T value)
    {
        add(value, value + 1);
    }


    void add(T start, T end)
    {
        if(start >= end)
            return;

        auto it = std::lower_bound(_ranges.begin(), _ranges.end(), start, [](const Interval &r, T v) { return r.second < v; });

        auto first = it;
        while(first != _ranges.begin() && (first - 1)->second >= start)
            --first;

        auto last = first;
        while(last != _ranges.end() && last->first <= end)
            ++last;

        if(first != last)
        {
            start = std::min(start, first->first);
            end = std::max(end, (last - 1)->second);

            auto pos = first - _ranges.begin();
            _ranges.erase(first, last);
            _ranges.insert(_ranges.begin() + pos, Interval{ start, end });
        }
        else
        {
            _ranges.insert(first, Interval{ start, end });
        }
    }


    void remove(T value)
    {
        remove(value, value + 1);
    }


    void remove(T start, T end)
    {
        if(start >= end)
            return;

        auto first = std::lower_bound(_ranges.begin(), _ranges.end(), start, [](const Interval &r, T v) { return r.second <= v; });

        auto last = first;
        while(last != _ranges.end() && last->first < end)
            ++last;

        if(first == last)
            return;

        std::vector<Interval> replacement;

        if(first->first < start)
            replacement.push_back(Interval{ first->first, start });

        if((last - 1)->second > end)
            replacement.push_back(Interval{ end, (last - 1)->second });

        auto pos = first - _ranges.begin();
        _ranges.erase(first, last);
        _ranges.insert(_ranges.begin() + pos, replacement.begin(), replacement.end());
    }


    bool contains(T value) const
    {
        auto it = std::upper_bound(_ranges.begin(), _ranges.end(), value, [](T v, const Interval &r) { return v < r.first; });
        if(it != _ranges.begin())
        {
            --it;
            return it->first <= value && value < it->second;
        }

        return false;
    }


    std::optional<T> next(T value) const
    {
        auto it = std::upper_bound(_ranges.begin(), _ranges.end(), value, [](T v, const Interval &r) { return v < r.first; });

        if(it != _ranges.begin())
        {
            auto prev = it - 1;
            if(prev->first <= value && value < prev->second && value + 1 < prev->second)
                return value + 1;
        }

        if(it != _ranges.end())
            return it->first;

        return std::nullopt;
    }


    std::optional<T> prev(T value) const
    {
        auto it = std::upper_bound(_ranges.begin(), _ranges.end(), value, [](T v, const Interval &r) { return v < r.first; });

        if(it != _ranges.begin())
        {
            auto p = it - 1;
            if(p->first <= value && value < p->second)
            {
                if(value > p->first)
                    return value - 1;

                if(p != _ranges.begin())
                    return (p - 1)->second - 1;

                return std::nullopt;
            }
            else if(value >= p->second)
            {
                return p->second - 1;
            }
        }

        return std::nullopt;
    }


    std::optional<T> first() const
    {
        if(_ranges.empty())
            return std::nullopt;

        return _ranges.front().first;
    }


    std::optional<T> last() const
    {
        if(_ranges.empty())
            return std::nullopt;

        return _ranges.back().second - 1;
    }


    const std::vector<Interval> &ranges() const
    {
        return _ranges;
    }


    bool empty() const
    {
        return _ranges.empty();
    }


    T count() const
    {
        T c = 0;
        for(auto &r : _ranges)
            c += r.second - r.first;
        return c;
    }


    std::optional<T> index(T value) const
    {
        T idx = 0;
        for(auto &r : _ranges)
        {
            if(value < r.first)
                break;

            if(value < r.second)
                return idx + (value - r.first);

            idx += r.second - r.first;
        }

        return std::nullopt;
    }

private:
    std::vector<Interval> _ranges;
};

}
