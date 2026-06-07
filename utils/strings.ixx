module;

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <codecvt>
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
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
}


export void trim_right_inplace(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
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
    std::transform(s.begin(), s.end(), std::back_inserter(str_uc), [](unsigned char c) { return std::toupper(c); });

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
    std::transform(s.begin(), s.end(), s.begin(), [r](unsigned char c) { return isprint(c) ? c : r; });
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
        if(std::isdigit((unsigned char)*it))
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


export template<typename T>
std::vector<std::pair<T, T>> string_to_ranges(const std::string &str)
{
    std::vector<std::pair<T, T>> ranges;

    std::istringstream iss(str);
    for(std::string range; std::getline(iss, range, ':');)
    {
        std::istringstream range_ss(range);
        std::string s;

        std::getline(range_ss, s, '-');
        auto start = str_to_int(s);
        if constexpr(std::is_unsigned_v<T>)
        {
            if(start < 0)
                throw_line("negative value not allowed for unsigned range type ({})", start);
        }

        std::getline(range_ss, s, '-');
        auto end = str_to_int(s);
        if constexpr(std::is_unsigned_v<T>)
        {
            if(end < 0)
                throw_line("negative value not allowed for unsigned range type ({})", end);
        }

        ranges.emplace_back((T)start, (T)end + 1);
    }

    return ranges;
}


export template<typename T, std::size_t N>
std::string_view to_string_view(const T (&arr)[N])
{
    return std::string_view(reinterpret_cast<const char *>(arr), N * sizeof(T));
}


export template<size_t M>
#pragma pack(push, 1)
struct pascal_string
{
    uint8_t length;
    char data[M];
};
#pragma pack(pop)


export template<size_t M>
std::string from_pascal_string(const pascal_string<M> &string)
{
    return std::string(string.data, std::min((size_t)string.length, M));
}


export std::string mac_roman_to_utf8(const std::string &string)
{
    std::string result;

    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;

    for(auto &c : string)
    {
        const uint8_t roman_code_point = c;

        constexpr uint8_t EXTENDED_CHARACTERS_START = 0x80;
        if(roman_code_point < EXTENDED_CHARACTERS_START)
        {
            // 7-bit ASCII
            result += (char)roman_code_point;
        }
        else
        {
            // Based on https://www.unicode.org/Public/MAPPINGS/VENDORS/APPLE/ROMAN.TXT
            constexpr std::array<uint16_t, 128> extended_characters_map = { 0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9,
                0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022,
                0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA,
                0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
                0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD,
                0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7 };
            uint16_t unicode_code_point = extended_characters_map[roman_code_point - EXTENDED_CHARACTERS_START];

            result += converter.to_bytes(unicode_code_point);
        }
    }

    return result;
}


}
