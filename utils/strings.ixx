module;

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "throw_line.hh"

export module utils.strings;

import utils.misc;



namespace gpsxre
{

export void trim_left_inplace(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c) { return !std::isspace(c); }));
}


export void trim_right_inplace(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](char c) { return !std::isspace(c); }).base(), s.end());
}


export void trim_inplace(std::string &s)
{
    trim_right_inplace(s);
    trim_left_inplace(s);
}


export std::string trim(std::string s)
{
    trim_inplace(s);
    return s;
}


export void erase_all_inplace(std::string &s, char c)
{
    s.erase(std::remove(s.begin(), s.end(), c), s.end());
}


export std::string erase_all(std::string s, char c)
{
    erase_all_inplace(s, c);
    return s;
}


export void extend_left_inplace(std::string &s, char c, size_t width)
{
    s = std::string(width - std::min(width, s.length()), c) + s;
}


export std::string extend_left(std::string s, char c, size_t width)
{
    extend_left_inplace(s, c, width);
    return s;
}


export void replace_all_inplace(std::string &s, std::string from, std::string to)
{
    for(size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.length())
        s.replace(pos, from.length(), to);
}


export std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
    replace_all_inplace(s, from, to);
    return s;
}


export std::string str_uppercase(const std::string &s)
{
    std::string str_uc;
    std::transform(s.begin(), s.end(), std::back_inserter(str_uc), [](char c) { return std::toupper(c); });

    return str_uc;
}


export std::string str_quoted_if_space(const std::string &s)
{
    const std::string quote("\"");

    return s.find(' ') == std::string::npos ? s : quote + s + quote;
}


export std::vector<std::string> tokenize(const std::string &str, const char *delimiters, const char *quotes)
{
    std::vector<std::string> tokens;

    std::set<char> delimiter;
    for(auto d = delimiters; *d != '\0'; ++d)
        delimiter.insert(*d);

    bool in = false;
    std::string::const_iterator s;
    for(auto it = str.begin(); it < str.end(); ++it)
    {
        if(in)
        {
            // quoted
            if(quotes != nullptr && *s == quotes[0])
            {
                if(*it == quotes[1])
                {
                    ++s;
                    tokens.emplace_back(s, it);
                    in = false;
                }
            }
            // unquoted
            else
            {
                if(delimiter.find(*it) != delimiter.end())
                {
                    tokens.emplace_back(s, it);
                    in = false;
                }
            }
        }
        else
        {
            if(delimiter.find(*it) == delimiter.end())
            {
                s = it;
                in = true;
            }
        }
    }

    // remaining entry
    if(in)
        tokens.emplace_back(s, str.end());

    return tokens;
}


export void replace_nonprint_inplace(std::string &s, char r)
{
    std::transform(s.begin(), s.end(), s.begin(), [r](char c) { return isprint(c) ? c : r; });
}


export std::string replace_nonprint(std::string s, char r)
{
    replace_nonprint_inplace(s, r);
    return s;
}


export std::string normalize_string(const std::string &s)
{
    std::string ns;

    auto tokens = tokenize(s, " ", nullptr);
    for(auto const &t : tokens)
        ns += replace_nonprint(t, '.') + ' ';

    if(!ns.empty())
        ns.pop_back();

    return ns;
}


export std::optional<uint64_t> str_to_uint64(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
    uint64_t value = 0;

    bool valid = false;
    for(auto it = str_begin; it != str_end; ++it)
    {
        if(std::isdigit(*it))
        {
            value = (value * 10) + (*it - '0');
            valid = true;
        }
        else
        {
            valid = false;
            break;
        }
    }

    return valid ? std::make_optional(value) : std::nullopt;
}


export std::optional<uint64_t> str_to_uint64(const std::string &str)
{
    return str_to_uint64(str.cbegin(), str.cend());
}


export std::optional<int64_t> str_to_int64(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
    // empty string
    auto it = str_begin;
    if(it == str_end)
        return std::nullopt;

    // preserve sign
    int64_t negative = 1;
    if(*it == '+')
        ++it;
    else if(*it == '-')
    {
        negative = -1;
        ++it;
    }

    if(auto value = str_to_uint64(it, str_end))
        return std::make_optional(negative * *value);

    return std::nullopt;
}


export std::optional<uint64_t> str_to_int64(const std::string &str)
{
    return str_to_int64(str.cbegin(), str.cend());
}


export int64_t str_to_int(const std::string &str)
{
    auto value = str_to_int64(str);
    if(!value)
        throw_line("string is not an integer number ({})", str);

    return *value;
}


export std::optional<double> str_to_double(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
    // empty string
    auto it = str_begin;
    if(it == str_end)
        return std::nullopt;

    // preserve sign
    double negative = 1;
    if(*it == '+')
        ++it;
    else if(*it == '-')
    {
        negative = -1;
        ++it;
    }

    auto dot = std::find(it, str_end, '.');

    if(auto whole = str_to_uint64(it, dot))
    {
        if(dot == str_end)
            return std::make_optional(negative * *whole);
        else
        {
            if(auto decimal = str_to_uint64(dot + 1, str_end))
            {
                auto fraction = *decimal / pow(10, digits_count(*decimal));

                return std::make_optional(negative * (*whole + fraction));
            }
        }
    }

    return std::nullopt;
}


export double str_to_double(const std::string &str)
{
    auto value = str_to_double(str.cbegin(), str.cend());
    if(!value)
        throw_line("string is not a double number ({})", str);

    return *value;
}


export std::vector<std::pair<int32_t, int32_t>> string_to_ranges(const std::string &str)
{
    std::vector<std::pair<int32_t, int32_t>> ranges;

    std::istringstream iss(str);
    for(std::string range; std::getline(iss, range, ':');)
    {
        std::istringstream range_ss(range);
        std::string s;

        std::getline(range_ss, s, '-');
        uint32_t lba_start = str_to_int(s);

        std::getline(range_ss, s, '-');
        uint32_t lba_end = str_to_int(s) + 1;

        ranges.emplace_back(lba_start, lba_end);
    }

    return ranges;
}


export std::string ranges_to_string(const std::vector<std::pair<int32_t, int32_t>> &ranges)
{
    std::string str;

    for(auto const &r : ranges)
        str += std::format("{}-{}:", r.first, r.second - 1);

    if(!str.empty())
        str.pop_back();

    return str;
}


}
